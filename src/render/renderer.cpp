/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "render/renderer.h"

#include "plugin/plugin.h"
#include "render/context.h"
#include "render/frame_targets.h"
#include "render/shader_pass.h"
#include "util/logging.h"
#include "util/paths.h"

#include <fstream>
#include <sstream>

namespace ReaShader
{
	namespace
	{
		gpu::CompiledShader compileDefaultShader()
		{
			std::ifstream file(util::paths::assetsDir() / "shaders" / "default.frag");
			if (!file)
				throw std::runtime_error("Missing assets/shaders/default.frag");
			std::stringstream source;
			source << file.rdbuf();
			return gpu::compileShader(source.str(), "default.frag");
		}
	} // namespace

	ReaShaderRenderer::ReaShaderRenderer(ReaShaderPlugin* reaShaderPlugin) : plugin(reaShaderPlugin)
	{
	}

	ReaShaderRenderer::~ReaShaderRenderer()
	{
		shutdown();
	}

	// -------- lifecycle --------

	void ReaShaderRenderer::init()
	{
		std::lock_guard lock(frameMutex);
		if (context && !failed)
			return; // already up
		_teardown(); // after a failure: start over
		try
		{
			context = std::make_unique<gpu::Context>();
			context->createInstance();

			std::vector<std::string> names = context->deviceNames();
			if (names.empty())
				throw std::runtime_error("No GPU with Vulkan 1.3 support found");
			plugin->setRenderingDevicesList(names);

			int index = plugin->getRenderingDeviceIndex();
			if (index < 0 || (size_t)index >= names.size())
			{
				index = 0; // the saved GPU is gone
				plugin->setRenderingDeviceIndex(index);
			}

			if (!shader)
				shader = std::make_unique<gpu::CompiledShader>(compileDefaultShader());

			_createDevice(index);
			failed = false;
		}
		catch (const std::exception& e)
		{
			LOG(e, toFile | toConsole | toBox, "ReaShaderRenderer", "Initialization failed",
				"Video passes through unchanged");
			_teardown();
			failed = true;
		}
	}

	void ReaShaderRenderer::shutdown()
	{
		std::lock_guard lock(frameMutex);
		_teardown();
	}

	void ReaShaderRenderer::_teardown()
	{
		if (!context)
			return;
		_destroyDevice();
		context->destroyInstance();
		context.reset();
	}

	void ReaShaderRenderer::changeRenderingDevice(int index)
	{
		std::lock_guard lock(frameMutex);
		if (!context || !context->hasDevice())
			return; // picked up by the next init()

		if (index < 0 || (size_t)index >= context->deviceNames().size())
			index = 0;

		try
		{
			_destroyDevice();
			_createDevice(index);
			failed = false;
		}
		catch (const std::exception& e)
		{
			LOG(e, toFile | toConsole | toBox, "ReaShaderRenderer", "Rendering device change failed",
				"Video passes through unchanged");
			failed = true;
		}
	}

	// GPU resources that depend on the device; the frame targets are created by the first frame
	void ReaShaderRenderer::_createDevice(int index)
	{
		context->createDevice((size_t)index);
		_installShader();
	}

	void ReaShaderRenderer::_destroyDevice()
	{
		if (!context->hasDevice())
			return;
		if (shaderPass)
			shaderPass->destroy(*context);
		if (targets)
			targets->destroy(*context);
		shaderPass.reset();
		targets.reset();
		context->destroyDevice();
	}

	// -------- shader --------

	void ReaShaderRenderer::_installShader()
	{
		auto newPass = std::make_unique<gpu::ShaderPass>();
		try
		{
			newPass->create(*context, *shader);
		}
		catch (...)
		{
			newPass->destroy(*context);
			throw;
		}
		if (targets)
			newPass->bindInput(*context, targets->input.view);

		// frames render one at a time and wait for the GPU, so the old pass is idle
		if (shaderPass)
			shaderPass->destroy(*context);
		shaderPass = std::move(newPass);
		frameCount = 0;
	}

	void ReaShaderRenderer::changeShader(const std::string& source, const std::string& name, StatusCallback onStatus,
										 StatusCallback onError, std::function<void()> onSuccess)
	{
		onStatus("Compiling...");

		auto compiled = std::make_unique<gpu::CompiledShader>();
		try
		{
			*compiled = source.empty() ? compileDefaultShader() : gpu::compileShader(source, name);
		}
		catch (const std::exception& e)
		{
			onError(e.what());
			return;
		}

		std::vector<Parameters::Param> params;
		for (const gpu::ShaderParamField& field : compiled->params)
		{
			Parameters::Param param;
			param.name = field.name;
			param.label = field.label;
			param.defaultValue = field.defaultValue;
			param.minValue = field.minValue;
			param.maxValue = field.maxValue;
			param.automatable = true;
			params.push_back(std::move(param));
		}

		{
			std::lock_guard lock(frameMutex);
			std::unique_ptr<gpu::CompiledShader> previous = std::move(shader);
			shader = std::move(compiled);

			if (context && context->hasDevice())
			{
				try
				{
					_installShader();
				}
				catch (const std::exception& e)
				{
					shader = std::move(previous); // keep rendering with the old one
					onError(e.what());
					return;
				}
			}
		}

		plugin->setShaderParams(std::move(params));
		onSuccess();
	}

	// -------- frames --------

	bool ReaShaderRenderer::renderFrame(const FrameView& input, const FrameView& output, const FrameInputs& inputs)
	{
		std::unique_lock lock(frameMutex, std::try_to_lock);
		if (!lock || failed || !shaderPass)
			return false;

		try
		{
			if (!targets || !targets->fits(input, output))
			{
				if (targets)
					targets->destroy(*context);
				targets = std::make_unique<gpu::FrameTargets>();
				targets->create(*context, input, output);
				shaderPass->bindInput(*context, targets->input.view);
			}

			// shader params follow the default ones in the plugin's param list
			float shaderParams[Parameters::ParamList::maxCount];
			size_t shaderParamCount = 0;
			for (size_t id = Parameters::DefaultCount; id < inputs.paramCount; id++)
				shaderParams[shaderParamCount++] = (float)inputs.paramValues[id];

			gpu::ShaderInputs shaderInputs{};
			shaderInputs.resolution[0] = (float)input.width;
			shaderInputs.resolution[1] = (float)input.height;
			shaderInputs.time = (float)inputs.time;
			shaderInputs.frameRate = (float)inputs.frameRate;
			shaderInputs.frame = frameCount++;
			shaderInputs.videoParam =
				Parameters::VideoParam < inputs.paramCount ? (float)inputs.paramValues[Parameters::VideoParam] : 0.0f;

			targets->writeInput(*context, input);
			shaderPass->writeParams(*context, shaderParams, shaderParamCount);

			VkCommandBuffer commandBuffer = context->beginCommands();
			targets->recordUpload(commandBuffer);
			shaderPass->record(commandBuffer, targets->output, shaderInputs);
			targets->recordDownload(commandBuffer);
			context->submitAndWait();

			targets->readOutput(*context, output);
			return true;
		}
		catch (const std::exception& e)
		{
			LOG(e, toFile | toConsole | toBox, "ReaShaderRenderer", "Rendering failed",
				"Video passes through unchanged until the plugin is re-activated");
			failed = true;
			return false;
		}
	}
} // namespace ReaShader
