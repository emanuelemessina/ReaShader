/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "support/support.h"

#include "util/paths.h"

#include <doctest/doctest.h>

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

	// -------- GPU --------

	namespace
	{
		std::unique_ptr<gpu::Context> sharedContext;

		// validation layer messages, as logged by gpu::Context into <test binary dir>/rs.log (one line each, first
		// line of the message only; the file is written with a flush per message)
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
		if (!sharedContext)
		{
			sharedContext = std::make_unique<gpu::Context>();
			sharedContext->createInstance();
		}
		gpu::Context& context = *sharedContext;

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
		if (inputs.pass)
		{
			inputs.pass->bindInput(context, targets.input.view);
			inputs.pass->writeParams(context, inputs.params.data(), inputs.params.size());
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
		if (inputs.pass)
			inputs.pass->record(commandBuffer, targets.output, shaderInputs);
		else
			targets.recordInputToOutput(commandBuffer);
		if (inputs.scene)
			inputs.scene->record(commandBuffer, targets.output, inputs.time, inputs.frameRate);
		targets.recordDownload(commandBuffer);
		context.submitAndWait();
		targets.readOutput(context, out);
	}
} // namespace test
