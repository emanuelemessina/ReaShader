/**
 * @file
 * @brief The .cube parser (Adobe/Resolve), 1D and domain baking, the stored JSON form.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/lut_file.h"

#include "util/base64.h"

#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace ReaShader::gpu
{
	namespace
	{
		constexpr int kLutStoredVersion = 1;
		constexpr uint32_t kMax1DSize = 65536;

		using Rgb = std::array<float, 3>;

		// The file as written: one of the two tables, over its own domain
		struct CubeFile
		{
			std::string title;
			uint32_t size3D = 0;
			uint32_t size1D = 0;
			Rgb domainMin{ 0, 0, 0 };
			Rgb domainMax{ 1, 1, 1 };
			std::vector<float> rgb;
		};

		[[noreturn]] void fail(const std::string& fileName, int line, std::string_view message)
		{
			throw std::runtime_error(std::format("{}:{}: {}", fileName, line, message));
		}

		std::vector<std::string_view> splitWords(std::string_view line)
		{
			std::vector<std::string_view> words;
			size_t i = 0;
			while (i < line.size())
			{
				while (i < line.size() && std::isspace((unsigned char)line[i]))
					i++;
				size_t start = i;
				while (i < line.size() && !std::isspace((unsigned char)line[i]))
					i++;
				if (i > start)
					words.push_back(line.substr(start, i - start));
			}
			return words;
		}

		// Locale independent, unlike streams and strtof
		bool toFloat(std::string_view word, float& value)
		{
			auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
			return error == std::errc() && end == word.data() + word.size() && std::isfinite(value);
		}

		bool toSize(std::string_view word, uint32_t& value)
		{
			auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
			return error == std::errc() && end == word.data() + word.size();
		}

		CubeFile read(const std::string& text, const std::string& fileName)
		{
			CubeFile file;

			std::istringstream lines(text);
			std::string line;
			for (int number = 1; std::getline(lines, line); number++)
			{
				std::string_view content = line;
				content = content.substr(0, content.find('#'));
				std::vector<std::string_view> words = splitWords(content);
				if (words.empty())
					continue;

				std::string_view keyword = words[0];
				if (!std::isalpha((unsigned char)keyword[0]))
				{
					if (words.size() != 3)
						fail(fileName, number, "expected 3 numbers (R G B)");
					for (std::string_view word : words)
					{
						float value;
						if (!toFloat(word, value))
							fail(fileName, number, std::format("'{}' is not a number", word));
						file.rgb.push_back(value);
					}
					continue;
				}

				if (keyword == "TITLE")
				{
					std::string_view title = content.substr(content.find("TITLE") + 5);
					title = title.substr(std::min(title.find_first_not_of(" \t\""), title.size()));
					title = title.substr(0, title.find_last_not_of(" \t\r\"") + 1);
					file.title = title;
				}
				else if (keyword == "LUT_3D_SIZE" || keyword == "LUT_1D_SIZE")
				{
					bool is3D = keyword == "LUT_3D_SIZE";
					uint32_t maxSize = is3D ? kMaxLutSize : kMax1DSize;
					uint32_t size = 0;
					if (words.size() != 2 || !toSize(words[1], size) || size < 2 || size > maxSize)
						fail(fileName, number, std::format("{} must be a number from 2 to {}", keyword, maxSize));
					(is3D ? file.size3D : file.size1D) = size;
				}
				else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX")
				{
					Rgb& domain = keyword == "DOMAIN_MIN" ? file.domainMin : file.domainMax;
					if (words.size() != 4 || !toFloat(words[1], domain[0]) || !toFloat(words[2], domain[1]) ||
						!toFloat(words[3], domain[2]))
						fail(fileName, number, std::format("{} needs 3 numbers (R G B)", keyword));
				}
				else if (keyword == "LUT_1D_INPUT_RANGE" || keyword == "LUT_3D_INPUT_RANGE")
				{
					float min, max;
					if (words.size() != 3 || !toFloat(words[1], min) || !toFloat(words[2], max))
						fail(fileName, number, std::format("{} needs 2 numbers (min max)", keyword));
					file.domainMin = { min, min, min };
					file.domainMax = { max, max, max };
				}
				// other keywords (from other applications) don't change the table
			}

			if (!file.size3D && !file.size1D)
				throw std::runtime_error(fileName + ": no LUT_3D_SIZE or LUT_1D_SIZE");
			if (file.size3D && file.size1D)
				throw std::runtime_error(fileName + ": files with both a 1D and a 3D table are not supported");

			size_t expected = file.size3D ? size_t(file.size3D) * file.size3D * file.size3D : file.size1D;
			if (file.rgb.size() / 3 != expected)
				throw std::runtime_error(
					std::format("{}: {} entries, but the size needs {}", fileName, file.rgb.size() / 3, expected));

			for (size_t c = 0; c < 3; c++)
				if (file.domainMax[c] <= file.domainMin[c])
					throw std::runtime_error(fileName + ": DOMAIN_MAX must be above DOMAIN_MIN");

			return file;
		}

		// Where an input value (0..1) falls in the file's table, 0..1 over its domain
		float tablePosition(const CubeFile& file, size_t channel, float value)
		{
			float min = file.domainMin[channel];
			float max = file.domainMax[channel];
			return (std::clamp(value, min, max) - min) / (max - min);
		}

		float sample1D(const CubeFile& file, size_t channel, float position)
		{
			float x = position * float(file.size1D - 1);
			uint32_t i = std::min((uint32_t)x, file.size1D - 2);
			float t = x - float(i);
			return std::lerp(file.rgb[i * 3 + channel], file.rgb[(i + 1) * 3 + channel], t);
		}

		// Trilinear, like the GPU's sampler
		Rgb sample3D(const CubeFile& file, const Rgb& position)
		{
			uint32_t n = file.size3D;
			std::array<uint32_t, 3> base;
			Rgb t;
			for (size_t c = 0; c < 3; c++)
			{
				float x = position[c] * float(n - 1);
				base[c] = std::min((uint32_t)x, n - 2);
				t[c] = x - float(base[c]);
			}

			Rgb out{ 0, 0, 0 };
			for (uint32_t corner = 0; corner < 8; corner++)
			{
				float weight = 1;
				size_t index = 0;
				size_t stride = 1;
				for (size_t c = 0; c < 3; c++)
				{
					bool upper = corner >> c & 1;
					weight *= upper ? t[c] : 1 - t[c];
					index += (base[c] + (upper ? 1u : 0u)) * stride;
					stride *= n;
				}
				for (size_t c = 0; c < 3; c++)
					out[c] += weight * file.rgb[index * 3 + c];
			}
			return out;
		}

		LutData bake(const CubeFile& file)
		{
			LutData lut;
			lut.title = file.title;

			bool defaultDomain = file.domainMin == Rgb{ 0, 0, 0 } && file.domainMax == Rgb{ 1, 1, 1 };
			if (file.size3D && defaultDomain)
			{
				lut.size = file.size3D;
				lut.rgb = file.rgb;
				return lut;
			}

			lut.size = file.size3D ? file.size3D : kBakedLutSize;
			uint32_t n = lut.size;
			lut.rgb.reserve(size_t(n) * n * n * 3);
			for (uint32_t b = 0; b < n; b++)
				for (uint32_t g = 0; g < n; g++)
					for (uint32_t r = 0; r < n; r++)
					{
						Rgb input{ float(r) / float(n - 1), float(g) / float(n - 1), float(b) / float(n - 1) };
						Rgb position;
						for (size_t c = 0; c < 3; c++)
							position[c] = tablePosition(file, c, input[c]);

						Rgb out;
						if (file.size3D)
							out = sample3D(file, position);
						else
							for (size_t c = 0; c < 3; c++)
								out[c] = sample1D(file, c, position[c]);
						lut.rgb.insert(lut.rgb.end(), out.begin(), out.end());
					}
			return lut;
		}
	} // namespace

	LutData parseCube(const std::string& text, const std::string& fileName)
	{
		return bake(read(text, fileName));
	}

	LutData identityLut(uint32_t size)
	{
		LutData lut;
		lut.title = "Identity";
		lut.size = size;
		lut.rgb.reserve(size_t(size) * size * size * 3);
		float last = float(size - 1);
		for (uint32_t b = 0; b < size; b++)
			for (uint32_t g = 0; g < size; g++)
				for (uint32_t r = 0; r < size; r++)
					lut.rgb.insert(lut.rgb.end(), { float(r) / last, float(g) / last, float(b) / last });
		return lut;
	}

	// -------- stored form --------

	nlohmann::json toJson(const LutData& lut)
	{
		std::vector<uint8_t> bytes;
		bytes.reserve(lut.rgb.size() * 2);
		for (float value : lut.rgb)
		{
			uint16_t half = (uint16_t)glm::packHalf1x16(value);
			bytes.push_back(uint8_t(half & 0xff));
			bytes.push_back(uint8_t(half >> 8));
		}
		return { { "version", kLutStoredVersion },
				 { "title", lut.title },
				 { "size", lut.size },
				 { "data", util::base64::encode(bytes) } };
	}

	LutData lutFromJson(const nlohmann::json& stored)
	{
		if (stored.value("version", 0) != kLutStoredVersion)
			throw std::runtime_error("Unsupported stored LUT version");

		LutData lut;
		lut.title = stored.value("title", "");
		lut.size = stored.at("size").get<uint32_t>();
		if (lut.size < 2 || lut.size > kMaxLutSize)
			throw std::runtime_error("Invalid stored LUT size");

		std::vector<uint8_t> bytes = util::base64::decode(stored.at("data").get<std::string>());
		size_t count = size_t(lut.size) * lut.size * lut.size * 3;
		if (bytes.size() != count * 2)
			throw std::runtime_error("Stored LUT data doesn't match its size");

		lut.rgb.resize(count);
		for (size_t i = 0; i < count; i++)
			lut.rgb[i] = glm::unpackHalf1x16(uint16_t(bytes[2 * i] | bytes[2 * i + 1] << 8));
		return lut;
	}
} // namespace ReaShader::gpu
