/**
 * @file
 * @brief Helpers shared by the host test cases: problem checks, frames, project states.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "host/reaper.h"
#include "host/video.h"
#include "support/support.h"

#include "render/lut_file.h"
#include "render/shader_compiler.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>

namespace test
{
	// every host test ends with this: nothing the host would consider wrong happened
	inline void checkNoProblems(const host::Reaper& reaper)
	{
		for (const std::string& problem : reaper.problems())
			FAIL_CHECK(problem);
	}

	// a frame with a different color per pixel, rows padded like the host's
	inline host::Frame gradientFrame(int width, int height)
	{
		host::Frame frame(width, height, host::rowspanFor(width));
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
			{
				uint8_t* p = frame.at(x, y);
				p[0] = (uint8_t)x, p[1] = (uint8_t)y, p[2] = (uint8_t)(x + y), p[3] = 255;
			}
		return frame;
	}

	// one color everywhere
	inline host::Frame solidFrame(int width, int height, uint8_t b, uint8_t g, uint8_t r)
	{
		host::Frame frame(width, height, host::rowspanFor(width));
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
			{
				uint8_t* p = frame.at(x, y);
				p[0] = b, p[1] = g, p[2] = r, p[3] = 255;
			}
		return frame;
	}

	// color channels of `out` further than 1 from expected(the same channel of `in`, 0..255); alpha must be unchanged
	inline int mismatches(const host::Frame& in, const host::Frame& out, const std::function<double(double)>& expected)
	{
		if (out.width != in.width || out.height != in.height)
			return in.width * in.height;
		int count = 0;
		for (int y = 0; y < in.height; y++)
			for (int x = 0; x < in.width; x++)
				for (int c = 0; c < 4; c++)
				{
					int source = in.at(x, y)[c];
					double want = c == 3 ? source : std::clamp(expected(source), 0.0, 255.0);
					count += std::abs(out.at(x, y)[c] - want) > 1.0;
				}
		return count;
	}

	// brightness.frag at `brightness`: out = in + brightness * 0.5
	inline double brighten(double value, double brightness)
	{
		return value + brightness * 0.5 * 255;
	}

	// channels of `out` that differ from brightness.frag applied to `in`
	inline int brightnessMismatches(const host::Frame& in, const host::Frame& out, double brightness)
	{
		return mismatches(in, out, [&](double v) { return brighten(v, brightness); });
	}

	// an example shader compiled here, stored as the plugin stores it (it would have compiled it on upload)
	inline nlohmann::json compiledExample(const std::string& exampleShader)
	{
		return ReaShader::gpu::toJson(
			ReaShader::gpu::compileShader(readFile(repoPath("src/shaders/examples") / exampleShader), exampleShader));
	}

	// A shader node of a project's chain: an example shader, optionally bypassed, optionally with a LUT (its
	// iChannel1). Its name is the file's stem; its params are named "<uid>/<member>".
	inline nlohmann::json shaderNode(uint32_t uid, const std::string& exampleShader, bool bypass = false,
									 const std::string& lutName = "", const ReaShader::gpu::LutData* lut = nullptr)
	{
		return { { "uid", uid },
				 { "kind", "shader" },
				 { "name", std::filesystem::path(exampleShader).stem().string() },
				 { "bypass", bypass },
				 { "data", compiledExample(exampleShader) },
				 { "lut",
				   { { "name", lutName }, { "data", lut ? ReaShader::gpu::toJson(*lut) : nlohmann::json() } } } };
	}

	// A LUT node of a project's chain, stored as the plugin stores it (it would have parsed it on upload).
	// Its Mix param is named "<uid>/mix".
	inline nlohmann::json lutNode(uint32_t uid, const std::string& name, const ReaShader::gpu::LutData& lut,
								  bool bypass = false)
	{
		return { { "uid", uid },
				 { "kind", "lut" },
				 { "name", name },
				 { "bypass", bypass },
				 { "data", ReaShader::gpu::toJson(lut) } };
	}

	// A project state as the plugin saves it: a chain of nodes (shaderNode, lutNode), param values by name,
	// the logo on or off
	inline std::string projectState(nlohmann::json chain, nlohmann::json params = nlohmann::json::object(),
									bool logo = false)
	{
		return nlohmann::json{
			{ "version", 4 }, { "params", params }, { "device", 0 }, { "logo", logo }, { "chain", chain }
		}.dump();
	}

	// The LUT part of a version 3 project state (before chains)
	inline nlohmann::json v3ProjectLut(const std::string& name, const ReaShader::gpu::LutData& lut,
									   const std::string& mode)
	{
		return { { "name", name }, { "mode", mode }, { "data", ReaShader::gpu::toJson(lut) } };
	}

	// A version 3 project state (before chains): optionally an example shader, the logo on or off, param
	// values by name, and a LUT (v3ProjectLut) with its mode
	inline std::string v3ProjectState(const std::string& exampleShader, bool logo,
									  nlohmann::json params = nlohmann::json::object(),
									  nlohmann::json lut = { { "name", "" }, { "mode", "after" }, { "data", nullptr } })
	{
		nlohmann::json compiled;
		std::string name;
		if (!exampleShader.empty())
		{
			compiled = compiledExample(exampleShader);
			name = std::filesystem::path(exampleShader).stem().string();
		}
		return nlohmann::json{ { "version", 3 },
							   { "params", params },
							   { "device", 0 },
							   { "logo", logo },
							   { "shader", { { "name", name }, { "compiled", compiled } } },
							   { "lut", lut } }
			.dump();
	}
} // namespace test
