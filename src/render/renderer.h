/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "render/frame_view.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace ReaShader
{
	class ReaShaderPlugin;

	namespace gpu
	{
		struct Context;
		struct FrameTargets;
		struct CompiledShader;
		class ShaderPass;
	} // namespace gpu

	// Renders REAPER's video frames through the current shader on the GPU.
	// - every public function holds frameMutex; renderFrame only tries it, so REAPER's video thread
	//   never waits (the frame passes through instead)
	// - never throws: errors are logged, or reported through the callbacks
	class ReaShaderRenderer
	{
	  public:
		explicit ReaShaderRenderer(ReaShaderPlugin* plugin);
		~ReaShaderRenderer();

		// Vulkan instance + the plugin's rendering device + the current shader, if any.
		// No-op when already up; after a failure it starts over.
		void init();
		void shutdown();

		void changeRenderingDevice(int index);

		// Makes `shader` the current one (installed now, or by the next init()) and replaces the plugin's
		// shader params. Returns an error message, empty on success; on error the previous shader stays.
		std::string setShader(const gpu::CompiledShader& shader);
		// no shader: frames pass through unchanged
		void clearShader();

		struct FrameInputs
		{
			double time;
			double frameRate;
			const double* paramValues; // all the plugin's params, by id
			size_t paramCount;
		};
		// false: nothing rendered (inactive, busy, failed or no shader), the caller passes the input through
		bool renderFrame(const FrameView& input, const FrameView& output, const FrameInputs& inputs);

	  private:
		void _createDevice(int index);
		void _destroyDevice();
		void _teardown(); // device + instance
		void _installShader(); // builds the pass for `shader`

		ReaShaderPlugin* plugin;

		std::mutex frameMutex; // guards everything below
		std::unique_ptr<gpu::Context> context;
		std::unique_ptr<gpu::FrameTargets> targets;
		std::unique_ptr<gpu::ShaderPass> shaderPass;
		std::unique_ptr<gpu::CompiledShader> shader; // kept to rebuild the pass on a device change
		bool failed = false;						 // a Vulkan error stops rendering until the next init()
		int32_t frameCount = 0;
	};
} // namespace ReaShader
