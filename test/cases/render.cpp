/**
 * @file
 * @brief Unit tests: the renderer's building blocks on every GPU, checked pixel by pixel.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/support.h"

#include "render/shader_compiler.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>

using namespace ReaShader;

TEST_SUITE("render")
{
	TEST_CASE("an example shader at an odd width with padded rows")
	{
		// brightness.frag: out = in + brightness * 0.5, alpha untouched
		gpu::CompiledShader shader = gpu::compileShader(
			test::readFile(test::repoPath("src/shaders/examples/brightness.frag")), "brightness.frag");
		REQUIRE(shader.params.size() == 1);
		CHECK(shader.params[0].label == "Brightness");

		test::TestFrame input(1917, 37, 64), output(1917, 37, 128);
		for (int y = 0; y < input.height; y++)
			for (int x = 0; x < input.width; x++)
			{
				uint8_t* p = input.at(x, y);
				p[0] = (uint8_t)(x % 200), p[1] = (uint8_t)(y * 3), p[2] = 40, p[3] = 255;
			}

		test::forEachGpu([&](gpu::Context& context) {
			gpu::ShaderPass pass;
			pass.create(context, shader);
			gpu::FrameTargets targets;
			test::render(context, targets, input, output, { .pass = &pass, .params = { 0.2f } });

			int mismatches = 0;
			for (int y = 0; y < input.height; y++)
				for (int x = 0; x < input.width; x++)
					for (int c = 0; c < 4; c++)
					{
						int source = input.at(x, y)[c];
						int expected = c == 3 ? source : std::min(255, (int)(source + 0.2 * 0.5 * 255 + 0.5));
						mismatches += std::abs(output.at(x, y)[c] - expected) > 1;
					}
			CHECK(mismatches == 0);

			pass.destroy(context);
			targets.destroy(context);
		});
	}

	TEST_CASE("Params values reach the shader, in B,G,R,A memory order")
	{
		gpu::CompiledShader shader = gpu::compileShader(
			"uniform Params { float amount; vec3 tint; };\nvoid main() { fragColor = vec4(tint, amount); }\n",
			"custom.glsl");

		test::TestFrame input(64, 8, 0), output(64, 8, 0);

		test::forEachGpu([&](gpu::Context& context) {
			gpu::ShaderPass pass;
			pass.create(context, shader);
			gpu::FrameTargets targets;
			test::render(context, targets, input, output, { .pass = &pass, .params = { 0.5f, 1.0f, 0.0f, 0.25f } });

			// B = tint.z = 0.25, G = tint.y = 0, R = tint.x = 1, A = amount = 0.5
			uint8_t* p = output.at(0, 0);
			CHECK(std::abs(p[0] - 64) <= 1);
			CHECK(p[1] == 0);
			CHECK(p[2] == 255);
			CHECK(std::abs(p[3] - 128) <= 1);

			pass.destroy(context);
			targets.destroy(context);
		});
	}

	TEST_CASE("params past the given values get their defaults")
	{
		gpu::CompiledShader shader = gpu::compileShader("//@param b 'B' 0.25\n//@param g 'G' 0.5\n"
														"uniform Params { float b; float g; };\n"
														"void main() { fragColor = vec4(0, g, b, 1); }\n",
														"defaults.glsl");

		test::TestFrame input(16, 4, 0), output(16, 4, 0);

		test::forEachGpu([&](gpu::Context& context) {
			gpu::ShaderPass pass;
			pass.create(context, shader);
			gpu::FrameTargets targets;
			// only b is given (as the plugin does while a new shader's params wait for the host's rescan)
			test::render(context, targets, input, output, { .pass = &pass, .params = { 1.0f } });

			uint8_t* p = output.at(0, 0);
			CHECK(p[0] == 255);				 // b = 1, given
			CHECK(std::abs(p[1] - 128) <= 1); // g = 0.5, its default

			pass.destroy(context);
			targets.destroy(context);
		});
	}

	TEST_CASE("the logo scene draws over a plain copy of the frame")
	{
		test::TestFrame input(640, 360, 0), output(640, 360, 0);
		for (size_t i = 0; i < input.bytes.size(); i += 4)
			input.bytes[i] = 10, input.bytes[i + 1] = 20, input.bytes[i + 2] = 30, input.bytes[i + 3] = 255;

		test::forEachGpu([&](gpu::Context& context) {
			gpu::Scene scene;
			scene.create(context);
			gpu::FrameTargets targets;
			test::render(context, targets, input, output, { .scene = &scene });

			int changed = 0;
			for (size_t i = 0; i < output.bytes.size(); i += 4)
				changed += output.bytes[i] != 10 || output.bytes[i + 1] != 20 || output.bytes[i + 2] != 30;
			CHECK(changed > 1000);			   // the logo is there...
			CHECK(changed < 640 * 360 / 2);	   // ...in the middle
			uint8_t* corner = output.at(0, 0); // the rest is the input
			CHECK(corner[0] == 10);
			CHECK(corner[1] == 20);
			CHECK(corner[2] == 30);

			targets.destroy(context);
			scene.destroy(context);
		});
	}
}
