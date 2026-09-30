/**
 * @file
 * @brief ReaShaderRenderer: renders REAPER's frames on the GPU, never throws.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/frame_view.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ReaShader
{
	class ReaShaderPlugin;

	namespace gpu
	{
		struct Context;
		struct FrameTargets;
		struct CompiledShader;
		struct LutData;
		struct Lut;
		class Scene;
	} // namespace gpu

	// Renders REAPER's video frames through a chain of shaders and LUTs on the GPU.
	// - every public function holds frameMutex; renderFrame only tries it, so REAPER's video thread
	//   never waits (the frame passes through instead)
	// - never throws: errors are logged, or returned (setChain)
	class ReaShaderRenderer
	{
	  public:
		// One step of the chain: a shader or a LUT.
		// Its content is shared and never changes: a node whose uid and pointers are the same as in the current
		// chain keeps its GPU objects.
		struct ChainNode
		{
			uint32_t uid = 0; // identifies the node across setChain calls, unique in a chain
			std::shared_ptr<const gpu::CompiledShader> shader; // a shader node...
			std::shared_ptr<const gpu::LutData> lut;			// ...or a LUT node
			std::shared_ptr<const gpu::LutData> shaderLut;		// a shader node's iChannel1 (none: an identity)
			bool bypass = false;								// left out of the passes, keeps its GPU objects
			// the node's params in FrameInputs::paramValues: a shader's in the order of CompiledShader::params,
			// a LUT's Mix; params past FrameInputs::paramCount get their defaults
			size_t firstParam = 0;
			size_t paramCount = 0;
		};

		explicit ReaShaderRenderer(ReaShaderPlugin* plugin);
		~ReaShaderRenderer();

		// Vulkan instance + the plugin's rendering device + the current chain.
		// No-op when already up; after a failure it starts over.
		void init();
		void shutdown();

		void changeRenderingDevice(int index);

		// Makes `chain` the current one (installed now, or by the next init()), in order.
		// Returns an error message, empty on success; on error the previous chain stays.
		std::string setChain(std::vector<ChainNode> chain);

		// the spinning ReaShader logo over the video (the easter egg); off by default
		void setLogoEnabled(bool enabled);

		struct FrameInputs
		{
			double time;
			double frameRate;
			const double* paramValues; // the plugin's params, by index in the param list
			size_t paramCount;
		};
		// false: nothing rendered (inactive, busy, failed, or no pass and no logo): the caller passes the input through
		bool renderFrame(const FrameView& input, const FrameView& output, const FrameInputs& inputs);

	  private:
		struct NodeObjects; // a node's GPU objects

		void _createDevice(int index);
		void _destroyDevice();
		void _teardown(); // device + instance
		void _createScene();

		ReaShaderPlugin* plugin;

		std::mutex frameMutex; // guards everything below
		std::unique_ptr<gpu::Context> context;
		std::unique_ptr<gpu::FrameTargets> targets;
		std::vector<ChainNode> chain;						   // kept to rebuild its GPU objects on a device change
		std::vector<std::unique_ptr<NodeObjects>> nodeObjects; // chain[i]'s, while there's a device
		std::unique_ptr<gpu::Lut> identityLut;				   // a shader's iChannel1 when it has no LUT
		std::unique_ptr<gpu::Scene> scene;					   // created while the logo is enabled
		bool logoEnabled = false;
		bool failed = false; // a Vulkan error stops rendering until the next init()
	};
} // namespace ReaShader
