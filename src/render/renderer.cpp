/**
 * @file
 * @brief ReaShaderRenderer: renders REAPER's frames on the GPU, never throws.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/renderer.h"

#include "plugin/params.h"
#include "plugin/plugin.h"
#include "render/context.h"
#include "render/frame_targets.h"
#include "render/lut.h"
#include "render/scene.h"
#include "render/shader_pass.h"
#include "util/logging.h"

namespace ReaShader
{
	// A node's GPU objects: its pass, and its LUT (a LUT node's table, or a shader node's iChannel1)
	struct ReaShaderRenderer::NodeObjects
	{
		std::unique_ptr<gpu::ShaderPass> shaderPass;
		std::unique_ptr<gpu::LutPass> lutPass;
		std::unique_ptr<gpu::Lut> lut; // none: a shader node samples the identity

		// What a node can take from the current chain's node with the same uid: the objects built from
		// the same content
		struct Reuse
		{
			NodeObjects* previous = nullptr;
			bool pass = false;
			bool lut = false;
		};

		static const gpu::LutData* table(const ChainNode& node)
		{
			return (node.shader ? node.shaderLut : node.lut).get();
		}

		static Reuse reuse(const ChainNode& node, const std::vector<ChainNode>& chain,
						   const std::vector<std::unique_ptr<NodeObjects>>& objects)
		{
			for (size_t i = 0; i < chain.size() && i < objects.size(); i++)
			{
				if (chain[i].uid != node.uid)
					continue;
				bool samePass = node.shader ? chain[i].shader == node.shader : !chain[i].shader;
				bool sameLut = table(node) && table(chain[i]) == table(node);
				return { objects[i].get(), samePass, sameLut };
			}
			return {};
		}

		// builds what `node` needs, except what `reused` will provide; cleans up after itself if it throws
		void create(gpu::Context& gpuContext, const ChainNode& node, const Reuse& reused)
		{
			try
			{
				if (node.shader && !reused.pass)
				{
					shaderPass = std::make_unique<gpu::ShaderPass>();
					shaderPass->create(gpuContext, *node.shader);
				}
				if (node.lut && !reused.pass)
				{
					lutPass = std::make_unique<gpu::LutPass>();
					lutPass->create(gpuContext);
				}
				if (table(node) && !reused.lut)
				{
					lut = std::make_unique<gpu::Lut>();
					lut->create(gpuContext, *table(node));
				}
			}
			catch (...)
			{
				destroy(gpuContext);
				throw;
			}
		}

		// takes the reused objects, once every node of the new chain is built
		void adopt(const Reuse& reused)
		{
			if (reused.pass)
			{
				shaderPass = std::move(reused.previous->shaderPass);
				lutPass = std::move(reused.previous->lutPass);
			}
			if (reused.lut)
				lut = std::move(reused.previous->lut);
		}

		void destroy(gpu::Context& gpuContext)
		{
			if (shaderPass)
				shaderPass->destroy(gpuContext);
			if (lutPass)
				lutPass->destroy(gpuContext);
			if (lut)
				lut->destroy(gpuContext);
			shaderPass.reset();
			lutPass.reset();
			lut.reset();
		}

		gpu::Pass* pass() const
		{
			return shaderPass ? static_cast<gpu::Pass*>(shaderPass.get()) : lutPass.get();
		}
	};

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

		identityLut = std::make_unique<gpu::Lut>();
		try
		{
			identityLut->create(*context, gpu::identityLut(gpu::kIdentityLutSize));
		}
		catch (...)
		{
			identityLut->destroy(*context);
			identityLut.reset();
			throw;
		}

		for (const ChainNode& node : chain)
		{
			nodeObjects.push_back(std::make_unique<NodeObjects>());
			nodeObjects.back()->create(*context, node, {});
		}
		if (logoEnabled)
			_createScene();
	}

	void ReaShaderRenderer::_destroyDevice()
	{
		if (!context->hasDevice())
			return;
		if (scene)
			scene->destroy(*context);
		for (auto& objects : nodeObjects)
			objects->destroy(*context);
		if (identityLut)
			identityLut->destroy(*context);
		if (targets)
			targets->destroy(*context);
		scene.reset();
		nodeObjects.clear();
		identityLut.reset();
		targets.reset();
		context->destroyDevice();
	}

	// -------- chain --------

	std::string ReaShaderRenderer::setChain(std::vector<ChainNode> newChain)
	{
		if (newChain.size() > Parameters::kMaxNodes)
			return "Too many nodes: at most " + std::to_string(Parameters::kMaxNodes);
		for (size_t i = 0; i < newChain.size(); i++)
		{
			if (!newChain[i].shader == !newChain[i].lut)
				return "A node must be a shader or a LUT";
			for (size_t j = 0; j < i; j++)
				if (newChain[j].uid == newChain[i].uid)
					return "Two nodes with uid " + std::to_string(newChain[i].uid);
		}

		std::lock_guard lock(frameMutex);
		if (!context || !context->hasDevice())
		{
			chain = std::move(newChain); // installed by the next init()
			return {};
		}

		// build the new chain's objects, then take what the current chain built from the same content
		std::vector<NodeObjects::Reuse> reused;
		std::vector<std::unique_ptr<NodeObjects>> newObjects;
		try
		{
			for (const ChainNode& node : newChain)
			{
				reused.push_back(NodeObjects::reuse(node, chain, nodeObjects));
				newObjects.push_back(std::make_unique<NodeObjects>());
				newObjects.back()->create(*context, node, reused.back());
			}
		}
		catch (const std::exception& e)
		{
			for (auto& objects : newObjects)
				objects->destroy(*context);
			return e.what(); // the current chain is untouched
		}
		for (size_t i = 0; i < newObjects.size(); i++)
			newObjects[i]->adopt(reused[i]);

		// frames render one at a time and wait for the GPU, so the old objects are idle
		for (auto& objects : nodeObjects)
			objects->destroy(*context);
		nodeObjects = std::move(newObjects);
		chain = std::move(newChain);
		return {};
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

		// the passes: the chain's nodes in order, bypassed ones left out
		gpu::Pass* passes[Parameters::kMaxNodes];
		size_t passCount = 0;
		for (size_t i = 0; i < nodeObjects.size(); i++)
		{
			if (!chain[i].bypass)
				passes[passCount++] = nodeObjects[i]->pass();
		}
		bool drawLogo = logoEnabled && scene;
		if (passCount == 0 && !drawLogo)
			return false;

		try
		{
			if (!targets || !targets->fits(input, output))
			{
				if (targets)
					targets->destroy(*context);
				targets = std::make_unique<gpu::FrameTargets>();
				targets->create(*context, input, output);
			}
			if (drawLogo)
				scene->prepare(*context, targets->extent);

			gpu::ShaderInputs shaderInputs{};
			shaderInputs.resolution[0] = (float)input.width;
			shaderInputs.resolution[1] = (float)input.height;
			shaderInputs.time = (float)inputs.time;
			shaderInputs.frameRate = (float)inputs.frameRate;

			targets->writeInput(*context, input);
			for (size_t i = 0; i < nodeObjects.size(); i++)
			{
				const ChainNode& node = chain[i];
				NodeObjects& objects = *nodeObjects[i];
				if (node.bypass)
					continue;

				// the node's values this frame; the rest (params waiting for the host's rescan) get defaults
				size_t count = node.firstParam < inputs.paramCount ? inputs.paramCount - node.firstParam : 0;
				if (count > node.paramCount)
					count = node.paramCount;
				const gpu::Lut& lut = objects.lut ? *objects.lut : *identityLut;

				if (objects.shaderPass)
				{
					// the host's values are 0..1 over each field's range (see Parameters::Param::toHost)
					float values[Parameters::kNodeSlots];
					if (count > Parameters::kNodeSlots)
						count = Parameters::kNodeSlots;
					if (count > node.shader->params.size())
						count = node.shader->params.size();
					for (size_t p = 0; p < count; p++)
					{
						const gpu::ShaderParamField& field = node.shader->params[p];
						values[p] = field.minValue +
									(float)inputs.paramValues[node.firstParam + p] * (field.maxValue - field.minValue);
					}
					objects.shaderPass->writeParams(*context, values, count);
					objects.shaderPass->bindLut(*context, lut.image.view);
				}
				else
				{
					objects.lutPass->bindLut(*context, lut.image.view);
					objects.lutPass->setAmount(count > 0 ? (float)inputs.paramValues[node.firstParam] : 1.0f); // Mix defaults to 1
				}
			}

			// upload -> passes (or a plain copy) -> logo -> download
			VkCommandBuffer commandBuffer = context->beginCommands();
			targets->recordUpload(commandBuffer);
			targets->recordPasses(*context, commandBuffer, std::span<gpu::Pass* const>(passes, passCount), shaderInputs);
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
