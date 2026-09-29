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

#include "render/shader_compiler.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
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

	// pixels of `out` that differ from brightness.frag applied to `in` (more than 1 per channel)
	inline int brightnessMismatches(const host::Frame& in, const host::Frame& out, double brightness)
	{
		if (out.width != in.width || out.height != in.height)
			return in.width * in.height;
		int mismatches = 0;
		for (int y = 0; y < in.height; y++)
			for (int x = 0; x < in.width; x++)
				for (int c = 0; c < 4; c++)
				{
					int source = in.at(x, y)[c];
					int expected = c == 3 ? source : std::min(255, (int)(source + brightness * 0.5 * 255 + 0.5));
					mismatches += std::abs(out.at(x, y)[c] - expected) > 1;
				}
		return mismatches;
	}

	// A project state as the plugin saves it: optionally with an example shader, compiled here (the plugin
	// would have compiled it on upload), and the logo on or off
	inline std::string projectState(const std::string& exampleShader, bool logo,
									nlohmann::json params = nlohmann::json::object())
	{
		nlohmann::json compiled;
		std::string name;
		if (!exampleShader.empty())
		{
			compiled = ReaShader::gpu::toJson(ReaShader::gpu::compileShader(
				readFile(repoPath("src/shaders/examples") / exampleShader), exampleShader));
			name = std::filesystem::path(exampleShader).stem().string();
		}
		return nlohmann::json{ { "version", 2 },
							   { "params", params },
							   { "device", 0 },
							   { "logo", logo },
							   { "shader", { { "name", name }, { "compiled", compiled } } } }
			.dump();
	}
} // namespace test
