/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// GPU test: runs the renderer's building blocks (FrameTargets, ShaderPass, Scene, the shader compiler)
// on every GPU of the machine and checks the output pixels. Seed of the test application
// (doc/proposals.md); not part of the plugin's build.

#include "render/context.h"
#include "render/frame_targets.h"
#include "render/scene.h"
#include "render/shader_pass.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ReaShader;

namespace
{
	int failures = 0;

	void expect(bool condition, const std::string& what)
	{
		if (!condition)
		{
			failures++;
			std::printf("  FAIL: %s\n", what.c_str());
		}
	}

	std::string readFile(const std::string& path)
	{
		std::ifstream file(path);
		std::stringstream content;
		content << file.rdbuf();
		return content.str();
	}

	std::string examplePath(const char* name)
	{
		return std::string(REPO_DIR) + "/src/shaders/examples/" + name;
	}

	// a BGRA frame, rows `rowBytes` apart, filled by `pixel(x, y, bgra)`
	struct TestFrame
	{
		int width, height, rowBytes;
		std::vector<uint8_t> bytes;

		TestFrame(int w, int h, int padding) : width(w), height(h), rowBytes(w * 4 + padding), bytes((size_t)rowBytes * h) {}
		uint8_t* at(int x, int y) { return &bytes[(size_t)y * rowBytes + (size_t)x * 4]; }
		FrameView view() { return { width, height, rowBytes, bytes.data() }; }
	};

	// upload -> (shader pass or plain copy) -> (scene) -> download
	void render(gpu::Context& context, gpu::FrameTargets& targets, TestFrame& input, TestFrame& output,
				gpu::ShaderPass* pass, const std::vector<float>& params, gpu::Scene* scene, double time)
	{
		FrameView in = input.view(), out = output.view();
		if (!targets.fits(in, out))
		{
			targets.destroy(context);
			targets.create(context, in, out);
		}
		if (pass)
		{
			pass->bindInput(context, targets.input.view);
			pass->writeParams(context, params.data(), params.size());
		}
		if (scene)
			scene->prepare(context, targets.extent);

		targets.writeInput(context, in);
		VkCommandBuffer commandBuffer = context.beginCommands();
		targets.recordUpload(commandBuffer);
		gpu::ShaderInputs inputs{ { (float)in.width, (float)in.height }, (float)time, 30.0f, 0 };
		if (pass)
			pass->record(commandBuffer, targets.output, inputs);
		else
			targets.recordInputToOutput(commandBuffer);
		if (scene)
			scene->record(commandBuffer, targets.output, time, 30.0);
		targets.recordDownload(commandBuffer);
		context.submitAndWait();
		targets.readOutput(context, out);
	}

	// -------- tests --------

	// brightness.frag at an odd width with padded rows: out = in + brightness * 0.5
	void testExampleShaderPixels(gpu::Context& context)
	{
		gpu::CompiledShader shader = gpu::compileShader(readFile(examplePath("brightness.frag")), "brightness.frag");
		expect(shader.params.size() == 1 && shader.params[0].label == "Brightness", "brightness.frag has one 'Brightness' param");

		TestFrame input(1917, 37, 64), output(1917, 37, 128);
		for (int y = 0; y < input.height; y++)
			for (int x = 0; x < input.width; x++)
			{
				uint8_t* p = input.at(x, y);
				p[0] = (uint8_t)(x % 200), p[1] = (uint8_t)(y * 3), p[2] = 40, p[3] = 255;
			}

		gpu::ShaderPass pass;
		pass.create(context, shader);
		gpu::FrameTargets targets;
		render(context, targets, input, output, &pass, { 0.2f }, nullptr, 0);

		int mismatches = 0;
		for (int y = 0; y < input.height; y++)
			for (int x = 0; x < input.width; x++)
				for (int c = 0; c < 4; c++)
				{
					int source = input.at(x, y)[c];
					int expected = c == 3 ? source : std::min(255, (int)(source + 0.2 * 0.5 * 255 + 0.5));
					mismatches += std::abs(output.at(x, y)[c] - expected) > 1;
				}
		expect(mismatches == 0, "brightness output matches (" + std::to_string(mismatches) + " mismatches)");

		pass.destroy(context);
		targets.destroy(context);
	}

	// Params reflection, UBO offsets, BGRA order, and the stored (JSON) form
	void testParamsAndStoredForm(gpu::Context& context)
	{
		gpu::CompiledShader shader = gpu::compileShader(
			"uniform Params { float amount; vec3 tint; };\nvoid main() { fragColor = vec4(tint, amount); }\n", "custom.glsl");
		expect(shader.params.size() == 4, "4 params (amount, tint.xyz)");

		gpu::CompiledShader stored = gpu::fromJson(nlohmann::json::parse(gpu::toJson(shader).dump()));
		expect(stored.spirv == shader.spirv && stored.params.size() == shader.params.size(), "JSON round trip");

		TestFrame input(64, 8, 0), output(64, 8, 0);
		gpu::ShaderPass pass;
		pass.create(context, stored);
		gpu::FrameTargets targets;
		render(context, targets, input, output, &pass, { 0.5f, 1.0f, 0.0f, 0.25f }, nullptr, 0);

		// memory is B,G,R,A: B = tint.z = 0.25, G = tint.y = 0, R = tint.x = 1, A = amount = 0.5
		uint8_t* p = output.at(0, 0);
		expect(std::abs(p[0] - 64) <= 1 && p[1] == 0 && p[2] == 255 && std::abs(p[3] - 128) <= 1, "Params values and channel order");

		pass.destroy(context);
		targets.destroy(context);
	}

	// annotations, the shipped examples, and compiler errors
	void testCompiler()
	{
		gpu::CompiledShader annotated = gpu::compileShader(
			"//@param amount 'The Amount' 2 0 10\nuniform Params { float amount; vec2 tint; };\nvoid main() { fragColor = vec4(tint, amount, 1); }\n",
			"annotated.glsl");
		const gpu::ShaderParamField& amount = annotated.params.at(0);
		expect(amount.label == "The Amount" && amount.defaultValue == 2 && amount.minValue == 0 && amount.maxValue == 10, "//@param annotation");
		expect(annotated.params.at(1).label == "tint.x" && annotated.params.at(1).defaultValue == 0.5f, "defaults without annotation");

		for (const char* example : { "brightness.frag", "tint.frag", "wave.frag", "pixelate.frag" })
		{
			try
			{
				gpu::compileShader(readFile(examplePath(example)), example);
			}
			catch (const std::exception& e)
			{
				expect(false, std::string(example) + " compiles: " + e.what());
			}
		}

		try
		{
			gpu::compileShader(readFile(std::string(REPO_DIR) + "/tests/shaders/broken.frag"), "broken.frag");
			expect(false, "broken.frag must not compile");
		}
		catch (const std::exception& e)
		{
			expect(std::string(e.what()).find("broken.frag:7") != std::string::npos, "error reported on the user's line 7");
		}

		try
		{
			gpu::compileShader("uniform sampler2D other;\nvoid main() { fragColor = texture(other, uv); }\n", "sampler.glsl");
			expect(false, "an extra sampler must be rejected");
		}
		catch (const std::exception&)
		{
		}
	}

	// the logo over a plain copy of the frame: drawn in the middle, the rest untouched
	void testScene(gpu::Context& context)
	{
		gpu::Scene scene;
		scene.create(context);

		TestFrame input(640, 360, 0), output(640, 360, 0);
		for (size_t i = 0; i < input.bytes.size(); i += 4)
			input.bytes[i] = 10, input.bytes[i + 1] = 20, input.bytes[i + 2] = 30, input.bytes[i + 3] = 255;

		gpu::FrameTargets targets;
		render(context, targets, input, output, nullptr, {}, &scene, 0);

		int changed = 0;
		for (size_t i = 0; i < output.bytes.size(); i += 4)
			changed += output.bytes[i] != 10 || output.bytes[i + 1] != 20 || output.bytes[i + 2] != 30;
		uint8_t* corner = output.at(0, 0);
		expect(changed > 1000 && changed < 640 * 360 / 2, "logo drawn (" + std::to_string(changed) + " pixels)");
		expect(corner[0] == 10 && corner[1] == 20 && corner[2] == 30, "frame outside the logo untouched");

		targets.destroy(context);
		scene.destroy(context);
	}
} // namespace

int main()
{
	try
	{
		testCompiler();

		gpu::Context context;
		context.createInstance();
		std::vector<std::string> gpus = context.deviceNames();
		for (size_t i = 0; i < gpus.size(); i++)
		{
			std::printf("GPU %zu: %s\n", i, gpus[i].c_str());
			context.createDevice(i);
			testExampleShaderPixels(context);
			testParamsAndStoredForm(context);
			testScene(context);
			context.destroyDevice();
		}
		context.destroyInstance();
	}
	catch (const std::exception& e)
	{
		std::printf("EXCEPTION: %s\n", e.what());
		return 2;
	}

	std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
	return failures ? 1 : 0;
}
