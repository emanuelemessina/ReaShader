/**
 * @file
 * @brief gpu::FrameTargets: the frame's buffers and images, and the copies between them.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"
#include "render/frame_view.h"
#include "render/pass.h"

#include <span>

namespace ReaShader::gpu
{
	// The frame's way through the GPU, recreated when the frame size changes:
	//   REAPER frame -> upload buffer -> input image -> (passes) -> output image -> readback buffer -> REAPER frame
	// Between passes the frame ping-pongs through two work images.
	struct FrameTargets
	{
		void create(Context& context, const FrameView& inputFrame, const FrameView& outputFrame);
		void destroy(Context& context);
		bool fits(const FrameView& inputFrame, const FrameView& outputFrame) const;

		// CPU side, before/after submitting
		void writeInput(Context& context, const FrameView& inputFrame);
		void readOutput(Context& context, const FrameView& outputFrame);

		// GPU side:
		// - upload: input ends up readable by shaders
		// - passes: in order, the first samples input, the last renders to output, the ones between go through
		//   the work images; binds each pass's input. No passes: output = input (a copy)
		// - either way output is left a color attachment
		// - download: output is copied back
		void recordUpload(VkCommandBuffer commandBuffer);
		void recordPasses(Context& context, VkCommandBuffer commandBuffer, std::span<Pass* const> passes,
						  const ShaderInputs& inputs);
		void recordDownload(VkCommandBuffer commandBuffer);

		VkExtent2D extent{};
		Image input;  // sampled by the first pass
		Image output; // rendered to by the last pass

	  private:
		void _recordInputToOutput(VkCommandBuffer commandBuffer);
		Image& _workImage(Context& context, size_t index); // created the first time a chain needs it

		Image work[2];
		Buffer upload;
		Buffer readback;
		int inputRowBytes = 0;
		int outputRowBytes = 0;
	};
} // namespace ReaShader::gpu
