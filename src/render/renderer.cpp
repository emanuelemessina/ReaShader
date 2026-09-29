/**
 * @file
 * @brief ReaShaderRenderer: renders REAPER's frames on the GPU, never throws.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/renderer.h"

#include "plugin/plugin.h"
#include "render/context.h"
#include "render/frame_targets.h"
#include "render/scene.h"
#include "render/shader_pass.h"
#include "util/logging.h"

namespace ReaShader
{
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
		if (shader)
			_installShader();
		if (logoEnabled)
			_createScene();
	}

	void ReaShaderRenderer::_destroyDevice()
	{
		if (!context->hasDevice())
			return;
		if (scene)
			scene->destroy(*context);
		if (shaderPass)
			shaderPass->destroy(*context);
		if (targets)
			targets->destroy(*context);
		scene.reset();
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

	std::string ReaShaderRenderer::setShader(const gpu::CompiledShader& compiled)
	{
		std::vector<Parameters::Param> params;
		for (const gpu::ShaderParamField& field : compiled.params)
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
			shader = std::make_unique<gpu::CompiledShader>(compiled);

			if (context && context->hasDevice())
			{
				try
				{
					_installShader();
				}
				catch (const std::exception& e)
				{
					shader = std::move(previous); // keep rendering with the old one
					return e.what();
				}
			}
		}

		plugin->setShaderParams(std::move(params));
		return {};
	}

	void ReaShaderRenderer::clearShader()
	{
		{
			std::lock_guard lock(frameMutex);
			if (shaderPass)
				shaderPass->destroy(*context);
			shaderPass.reset();
			shader.reset();
		}
		plugin->setShaderParams({});
	}

	// -------- logo scene --------

	void ReaShaderRenderer::setLogoEnabled(bool enabled)
	{
		std::lock_guard lock(frameMutex);
		logoEnabled = enabled;
		if (!enabled || scene || !context || !context->hasDevice())
			return;
		try
		{
			_createScene();
		}
		catch (const std::exception& e)
		{
			LOG(e, toFile | toConsole, "ReaShaderRenderer", "Logo scene failed", "The logo stays off");
		}
	}

	void ReaShaderRenderer::_createScene()
	{
		auto newScene = std::make_unique<gpu::Scene>();
		try
		{
			newScene->create(*context);
		}
		catch (...)
		{
			newScene->destroy(*context);
			throw;
		}
		scene = std::move(newScene);
	}

	// -------- frames --------

	bool ReaShaderRenderer::renderFrame(const FrameView& input, const FrameView& output, const FrameInputs& inputs)
	{
		std::unique_lock lock(frameMutex, std::try_to_lock);
		if (!lock || failed)
			return false;
		bool drawLogo = logoEnabled && scene;
		if (!shaderPass && !drawLogo)
			return false;

		try
		{
			if (!targets || !targets->fits(input, output))
			{
				if (targets)
					targets->destroy(*context);
				targets = std::make_unique<gpu::FrameTargets>();
				targets->create(*context, input, output);
				if (shaderPass)
					shaderPass->bindInput(*context, targets->input.view);
			}
			if (drawLogo)
				scene->prepare(*context, targets->extent);

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

			targets->writeInput(*context, input);
			if (shaderPass)
				shaderPass->writeParams(*context, shaderParams, shaderParamCount);

			// upload -> shader (or a plain copy) -> logo -> download
			VkCommandBuffer commandBuffer = context->beginCommands();
			targets->recordUpload(commandBuffer);
			if (shaderPass)
				shaderPass->record(commandBuffer, targets->output, shaderInputs);
			else
				targets->recordInputToOutput(commandBuffer);
			if (drawLogo)
				scene->record(commandBuffer, targets->output, inputs.time, inputs.frameRate);
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
