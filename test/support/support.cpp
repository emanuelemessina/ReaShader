/**
 * @file
 * @brief Helpers shared by the test cases: repo files, CPU frames, GPU runs.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/support.h"

#include "util/paths.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

using namespace ReaShader;

namespace test
{
	// -------- files --------

	std::filesystem::path repoPath(const std::string& relative)
	{
		return std::filesystem::path(REPO_DIR) / relative;
	}

	std::string readFile(const std::filesystem::path& path)
	{
		std::ifstream file(path);
		REQUIRE_MESSAGE(file.is_open(), "can't open ", path.string());
		std::stringstream content;
		content << file.rdbuf();
		return content.str();
	}

	// -------- frames --------

	TestFrame::TestFrame(int frameWidth, int frameHeight, int padding)
		: width(frameWidth), height(frameHeight), rowBytes(frameWidth * 4 + padding),
		  bytes((size_t)rowBytes * (size_t)frameHeight)
	{
	}

	uint8_t* TestFrame::at(int x, int y)
	{
		return &bytes[(size_t)y * (size_t)rowBytes + (size_t)x * 4];
	}

	FrameView TestFrame::view()
	{
		return { width, height, rowBytes, bytes.data() };
	}

	TestFrame gradient()
	{
		TestFrame frame(256, 4, 0);
		for (int y = 0; y < frame.height; y++)
			for (int x = 0; x < frame.width; x++)
			{
				uint8_t* p = frame.at(x, y);
				p[0] = (uint8_t)x, p[1] = (uint8_t)(255 - x), p[2] = (uint8_t)(x * 3), p[3] = 255;
			}
		return frame;
	}

	int mismatches(TestFrame& input, TestFrame& output, const std::function<double(double)>& expected, double tolerance)
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

	// -------- GPU --------

	namespace
	{
		std::unique_ptr<gpu::Context> sharedContext;

		gpu::Context& sharedInstance()
		{
			if (!sharedContext)
			{
				sharedContext = std::make_unique<gpu::Context>();
				sharedContext->createInstance();
			}
			return *sharedContext;
		}

		// destroys the device even when a REQUIRE throws out of the test body
		struct DeviceScope
		{
			gpu::Context& context;
			~DeviceScope()
			{
				context.destroyDevice();
			}
		};
	} // namespace

	void forEachGpu(const std::function<void(gpu::Context&)>& body)
	{
		gpu::Context& context = sharedInstance();

		std::vector<std::string> gpus = context.deviceNames();
		REQUIRE_MESSAGE(!gpus.empty(), "no GPU with Vulkan 1.3");

		for (size_t i = 0; i < gpus.size(); i++)
		{
			INFO("GPU ", i, ": ", gpus[i]);
			size_t knownMessages = validationMessages().size();
			{
				context.createDevice(i);
				DeviceScope scope{ context };
				body(context);
			}
			std::vector<std::string> messages = validationMessages();
			for (size_t m = knownMessages; m < messages.size(); m++)
				FAIL_CHECK(messages[m]);
		}
	}

	size_t gpuCount()
	{
		return sharedInstance().deviceNames().size();
	}

	// as logged by gpu::Context into <test binary dir>/rs.log (first line of the message only; the file is written
	// with a flush per message)
	std::vector<std::string> validationMessages()
	{
		std::vector<std::string> messages;
		std::ifstream log(util::paths::pluginDir() / "rs.log");
		std::string line;
		while (std::getline(log, line))
		{
			if (line.find("(Vulkan) Validation") != std::string::npos)
				messages.push_back(line);
		}
		return messages;
	}

	void shutdownGpu()
	{
		if (sharedContext)
			sharedContext->destroyInstance();
		sharedContext.reset();
	}

	void render(gpu::Context& context, gpu::FrameTargets& targets, TestFrame& input, TestFrame& output,
				const RenderInputs& inputs)
	{
		FrameView in = input.view(), out = output.view();
		if (!targets.fits(in, out))
		{
			targets.destroy(context);
			targets.create(context, in, out);
		}

		gpu::Lut identity;
		const gpu::Lut* shaderLut = inputs.shaderLut;
		if (!shaderLut)
		{
			identity.create(context, gpu::identityLut(gpu::kIdentityLutSize));
			shaderLut = &identity;
		}
		for (gpu::Pass* pass : inputs.passes)
			if (auto* shaderPass = dynamic_cast<gpu::ShaderPass*>(pass))
			{
				shaderPass->writeParams(context, inputs.params.data(), inputs.params.size());
				shaderPass->bindLut(context, shaderLut->image.view);
			}
		if (inputs.scene)
			inputs.scene->prepare(context, targets.extent);

		targets.writeInput(context, in);
		VkCommandBuffer commandBuffer = context.beginCommands();
		targets.recordUpload(commandBuffer);
		gpu::ShaderInputs shaderInputs{ { (float)in.width, (float)in.height },
										(float)inputs.time,
										(float)inputs.frameRate,
										0 };
		targets.recordPasses(context, commandBuffer, inputs.passes, shaderInputs);
		if (inputs.scene)
			inputs.scene->record(commandBuffer, targets.output, inputs.time, inputs.frameRate);
		targets.recordDownload(commandBuffer);
		context.submitAndWait();
		targets.readOutput(context, out);

		identity.destroy(context);
	}

	gpu::LutData invertLut()
	{
		gpu::LutData lut = gpu::identityLut(33);
		for (float& value : lut.rgb)
			value = 1 - value;
		return lut;
	}
} // namespace test
