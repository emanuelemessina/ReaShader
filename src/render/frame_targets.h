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

namespace ReaShader::gpu
{
	// The frame's way through the GPU, recreated when the frame size changes:
	//   REAPER frame -> upload buffer -> input image -> (passes) -> output image -> readback buffer -> REAPER frame
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
		// - passes leave output as a color attachment
		// - download: output is copied back
		void recordUpload(VkCommandBuffer commandBuffer);
		void recordInputToOutput(VkCommandBuffer commandBuffer); // stands in for passes: output = input
		void recordDownload(VkCommandBuffer commandBuffer);

		VkExtent2D extent{};
		Image input;  // sampled by the passes
		Image output; // rendered to by the last pass

	  private:
		Buffer upload;
		Buffer readback;
		int inputRowBytes = 0;
		int outputRowBytes = 0;
	};
} // namespace ReaShader::gpu
