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

namespace ReaShader
{
	class ReaShaderPlugin;

	namespace gpu
	{
		struct Context;
		struct FrameTargets;
		struct CompiledShader;
		class ShaderPass;
		struct LutData;
		struct Lut;
		class LutPass;
		class Scene;
	} // namespace gpu

	// Where the LUT applies, relative to the shader
	enum class LutMode
	{
		Before, // LUT pass -> shader
		After,	// shader -> LUT pass
		Shader, // no LUT pass: the shader samples it as iChannel1 (with no shader: like After)
	};

	// Renders REAPER's video frames through the current shader and LUT on the GPU.
	// - every public function holds frameMutex; renderFrame only tries it, so REAPER's video thread
	//   never waits (the frame passes through instead)
	// - never throws: errors are logged, or returned (setShader)
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

		// Makes `lut` the current one, like setShader. Returns an error message, empty on success.
		std::string setLut(const gpu::LutData& lut);
		void clearLut();
		void setLutMode(LutMode mode);

		// the spinning ReaShader logo over the video (the easter egg); off by default
		void setLogoEnabled(bool enabled);

		struct FrameInputs
		{
			double time;
			double frameRate;
			const double* paramValues; // all the plugin's params, by index in the param list (at least the fixed ones)
			size_t paramCount;
		};
		// false: nothing rendered (inactive, busy, failed, or no shader, LUT or logo): the caller passes the input through
		bool renderFrame(const FrameView& input, const FrameView& output, const FrameInputs& inputs);

	  private:
		void _createDevice(int index);
		void _destroyDevice();
		void _teardown(); // device + instance
		void _installShader(); // builds the pass for `shader`
		void _installLut();	   // uploads `lut`
		void _createLutPass(); // the LUT pass and the identity LUT
		void _createScene();

		ReaShaderPlugin* plugin;

		std::mutex frameMutex; // guards everything below
		std::unique_ptr<gpu::Context> context;
		std::unique_ptr<gpu::FrameTargets> targets;
		std::unique_ptr<gpu::ShaderPass> shaderPass;
		std::unique_ptr<gpu::CompiledShader> shader; // kept to rebuild the pass on a device change
		std::unique_ptr<gpu::LutData> lut;			 // kept to upload it again on a device change
		std::unique_ptr<gpu::Lut> lutImage;			 // `lut` on the device
		std::unique_ptr<gpu::Lut> identityLut;		 // the shader's iChannel1 when the LUT isn't the shader's
		std::unique_ptr<gpu::LutPass> lutPass;		 // created with the device
		LutMode lutMode = LutMode::After;
		std::unique_ptr<gpu::Scene> scene;			 // created while the logo is enabled
		bool logoEnabled = false;
		bool failed = false;						 // a Vulkan error stops rendering until the next init()
		int32_t frameCount = 0;
	};
} // namespace ReaShader
