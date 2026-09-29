/**
 * @file
 * @brief Base64 encoding and decoding.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "util/base64.h"

#include <stdexcept>

namespace util::base64
{
	namespace
	{
		constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		// -1 for a character outside the alphabet
		int valueOf(char c)
		{
			if (c >= 'A' && c <= 'Z')
				return c - 'A';
			if (c >= 'a' && c <= 'z')
				return c - 'a' + 26;
			if (c >= '0' && c <= '9')
				return c - '0' + 52;
			if (c == '+')
				return 62;
			if (c == '/')
				return 63;
			return -1;
		}
	} // namespace

	std::string encode(std::span<const uint8_t> bytes)
	{
		std::string text;
		text.reserve((bytes.size() + 2) / 3 * 4);
		for (size_t i = 0; i < bytes.size(); i += 3)
		{
			size_t remaining = bytes.size() - i;
			uint32_t chunk = uint32_t(bytes[i]) << 16;
			if (remaining > 1)
				chunk |= uint32_t(bytes[i + 1]) << 8;
			if (remaining > 2)
				chunk |= bytes[i + 2];

			text += kAlphabet[(chunk >> 18) & 63];
			text += kAlphabet[(chunk >> 12) & 63];
			text += remaining > 1 ? kAlphabet[(chunk >> 6) & 63] : '=';
			text += remaining > 2 ? kAlphabet[chunk & 63] : '=';
		}
		return text;
	}

	std::vector<uint8_t> decode(std::string_view text)
	{
		if (text.size() % 4 != 0)
			throw std::runtime_error("Invalid base64: length is not a multiple of 4");

		std::vector<uint8_t> bytes;
		bytes.reserve(text.size() / 4 * 3);
		for (size_t i = 0; i < text.size(); i += 4)
		{
			// '=' pads only the last group, at its end
			bool last = i + 4 == text.size();
			size_t padding = last ? size_t(text[i + 3] == '=') + size_t(text[i + 3] == '=' && text[i + 2] == '=') : 0;

			uint32_t chunk = 0;
			for (size_t j = 0; j < 4; j++)
			{
				int value = j >= 4 - padding ? 0 : valueOf(text[i + j]);
				if (value < 0)
					throw std::runtime_error("Invalid base64: unexpected character");
				chunk = chunk << 6 | uint32_t(value);
			}

			bytes.push_back(uint8_t(chunk >> 16));
			if (padding < 2)
				bytes.push_back(uint8_t(chunk >> 8 & 0xff));
			if (padding < 1)
				bytes.push_back(uint8_t(chunk & 0xff));
		}
		return bytes;
	}
} // namespace util::base64
