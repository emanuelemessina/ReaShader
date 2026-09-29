/**
 * @file
 * @brief Unit tests: the renderer's building blocks on every GPU, checked pixel by pixel.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/support.h"

#include "render/lut_file.h"
#include "render/shader_compiler.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

using namespace ReaShader;

namespace
{
	// B = x, G = 255 - x, R = 3x (wrapping), A = 255: every 8-bit value in every color channel
	test::TestFrame gradient()
	{
		test::TestFrame frame(256, 4, 0);
		for (int y = 0; y < frame.height; y++)
			for (int x = 0; x < frame.width; x++)
			{
				uint8_t* p = frame.at(x, y);
				p[0] = (uint8_t)x, p[1] = (uint8_t)(255 - x), p[2] = (uint8_t)(x * 3), p[3] = 255;
			}
		return frame;
	}

	// color channels further than `tolerance` from expected(input value, 0..255); alpha must be the input's
	int mismatches(test::TestFrame& input, test::TestFrame& output, const std::function<double(double)>& expected,
				   double tolerance = 1.0)
	{
		int count = 0;
		for (int y = 0; y < input.height; y++)
			for (int x = 0; x < input.width; x++)
				for (int c = 0; c < 4; c++)
				{
					int source = input.at(x, y)[c];
					double want = c == 3 ? source : std::clamp(expected(source), 0.0, 255.0);
					count += std::abs(output.at(x, y)[c] - want) > tolerance;
				}
		return count;
	}

	// 33 points per side: the hardware's interpolation weights (8-bit on some GPUs) stay far below one 8-bit step,
	// which a 2-point LUT like invert.cube would not guarantee
	gpu::LutData invertLut()
	{
		gpu::LutData lut = gpu::identityLut(33);
		for (float& value : lut.rgb)
			value = 1 - value;
		return lut;
	}

	gpu::CompiledShader brightnessShader()
	{
		return gpu::compileShader(test::readFile(test::repoPath("src/shaders/examples/brightness.frag")),
								  "brightness.frag");
	}

	double invert(double v)
	{
		return 255 - v;
	}

	// brightness.frag at 0.2: out = in + 0.2 * 0.5
	double brighten(double v)
	{
		return std::min(255.0, v + 0.2 * 0.5 * 255);
	}
} // namespace

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
			test::render(context, targets, input, output, { .passes = { &pass }, .params = { 0.2f } });

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
			test::render(context, targets, input, output, { .passes = { &pass }, .params = { 0.5f, 1.0f, 0.0f, 0.25f } });

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
			test::render(context, targets, input, output, { .passes = { &pass }, .params = { 1.0f } });

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

	TEST_CASE("a LUT pass: an identity keeps the frame, an inverting LUT inverts it, the amount blends")
	{
		test::TestFrame input = gradient(), output(256, 4, 0);
		gpu::LutData inverted = invertLut();

		test::forEachGpu([&](gpu::Context& context) {
			gpu::Lut identityLut, invertLut;
			identityLut.create(context, gpu::identityLut(33));
			invertLut.create(context, inverted);
			gpu::LutPass pass;
			pass.create(context);
			gpu::FrameTargets targets;

			pass.bindLut(context, identityLut.image.view);
			test::render(context, targets, input, output, { .passes = { &pass } });
			CHECK(mismatches(input, output, [](double v) { return v; }) == 0);

			pass.bindLut(context, invertLut.image.view);
			test::render(context, targets, input, output, { .passes = { &pass } });
			CHECK(mismatches(input, output, invert) == 0);

			pass.setAmount(0.5f);
			test::render(context, targets, input, output, { .passes = { &pass } });
			CHECK(mismatches(input, output, [](double v) { return (v + invert(v)) / 2; }) == 0);

			pass.destroy(context);
			targets.destroy(context);
			invertLut.destroy(context);
			identityLut.destroy(context);
		});
	}

	TEST_CASE("the LUT before or after a shader: the order of the passes")
	{
		test::TestFrame input = gradient(), output(256, 4, 0);
		gpu::LutData inverted = invertLut();
		gpu::CompiledShader brightness = brightnessShader();

		test::forEachGpu([&](gpu::Context& context) {
			gpu::Lut invertLut;
			invertLut.create(context, inverted);
			gpu::LutPass lutPass;
			lutPass.create(context);
			lutPass.bindLut(context, invertLut.image.view);
			gpu::ShaderPass shaderPass;
			shaderPass.create(context, brightness);
			gpu::FrameTargets targets;

			test::render(context, targets, input, output, { .passes = { &lutPass, &shaderPass }, .params = { 0.2f } });
			CHECK(mismatches(input, output, [](double v) { return brighten(invert(v)); }) == 0);

			test::render(context, targets, input, output, { .passes = { &shaderPass, &lutPass }, .params = { 0.2f } });
			CHECK(mismatches(input, output, [](double v) { return invert(brighten(v)); }) == 0);

			shaderPass.destroy(context);
			lutPass.destroy(context);
			targets.destroy(context);
			invertLut.destroy(context);
		});
	}

	TEST_CASE("a shader samples the LUT as iChannel1 through iLut (an identity by default)")
	{
		test::TestFrame input = gradient(), output(256, 4, 0);
		gpu::LutData inverted = invertLut();
		gpu::CompiledShader shader = gpu::compileShader(
			"void main() { vec4 c = texture(iChannel0, uv); fragColor = vec4(iLut(c.rgb), c.a); }\n", "ilut.glsl");

		test::forEachGpu([&](gpu::Context& context) {
			gpu::Lut invertLut;
			invertLut.create(context, inverted);
			gpu::ShaderPass pass;
			pass.create(context, shader);
			gpu::FrameTargets targets;

			test::render(context, targets, input, output, { .passes = { &pass } });
			CHECK(mismatches(input, output, [](double v) { return v; }) == 0);

			test::render(context, targets, input, output, { .passes = { &pass }, .shaderLut = &invertLut });
			CHECK(mismatches(input, output, invert) == 0);

			pass.destroy(context);
			targets.destroy(context);
			invertLut.destroy(context);
		});
	}

	TEST_CASE("four passes ping-pong through the work images")
	{
		test::TestFrame input = gradient(), output(256, 4, 0);
		gpu::LutData inverted = invertLut();
		gpu::CompiledShader brightness = brightnessShader();

		test::forEachGpu([&](gpu::Context& context) {
			gpu::Lut invertLut;
			invertLut.create(context, inverted);
			gpu::LutPass lutPasses[2];
			gpu::ShaderPass shaderPasses[2];
			for (int i = 0; i < 2; i++)
			{
				lutPasses[i].create(context);
				lutPasses[i].bindLut(context, invertLut.image.view);
				shaderPasses[i].create(context, brightness);
			}
			gpu::FrameTargets targets;

			// work[0], work[1], work[0] again (after the second pass sampled it), output
			test::render(context, targets, input, output,
						 { .passes = { &lutPasses[0], &shaderPasses[0], &lutPasses[1], &shaderPasses[1] },
						   .params = { 0.2f } });
			// two roundings to 8 bits in the work images
			CHECK(mismatches(input, output, [](double v) { return brighten(invert(brighten(invert(v)))); }, 1.5) == 0);

			for (int i = 0; i < 2; i++)
			{
				shaderPasses[i].destroy(context);
				lutPasses[i].destroy(context);
			}
			targets.destroy(context);
			invertLut.destroy(context);
		});
	}

	TEST_CASE("each frame samples its own input, not the previous frame's")
	{
		test::TestFrame first = gradient(), second = gradient(), output(256, 4, 0);
		for (uint8_t& byte : second.bytes)
			byte = (uint8_t)(255 - byte);
		gpu::CompiledShader brightness = brightnessShader();

		test::forEachGpu([&](gpu::Context& context) {
			gpu::ShaderPass pass;
			pass.create(context, brightness);
			gpu::FrameTargets targets;

			for (test::TestFrame* input : { &first, &second, &first })
			{
				test::render(context, targets, *input, output, { .passes = { &pass }, .params = { 0.2f } });
				int wrong = 0;
				for (int x = 0; x < 256; x++)
					for (int c = 0; c < 3; c++)
						wrong += std::abs(output.at(x, 0)[c] - brighten(input->at(x, 0)[c])) > 1.0;
				CHECK(wrong == 0);
			}

			pass.destroy(context);
			targets.destroy(context);
		});
	}
}
