/**
 * @file
 * @brief gpu::Pass: a fullscreen pass, the unit FrameTargets::recordPasses chains.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"
#include "render/shader_compiler.h"

#include <span>

namespace ReaShader::gpu
{
	// Samples one image and renders every pixel of another
	class Pass
	{
	  public:
		virtual ~Pass() = default;

		// The image the pass samples. It depends on the pass's place in the chain, so it's bound every frame
		// (legal: frames wait for the GPU, so the descriptor set isn't in use).
		virtual void bindInput(Context& context, VkImageView input) = 0;

		// renders to `target`, leaving it a color attachment
		virtual void record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs& inputs) = 0;
	};

	// fullscreen.vert: the vertex shader of every pass, one triangle covering the frame
	std::span<const uint32_t> fullscreenVertexSpirv();

	// Points a combined image sampler binding at `view` (the shared sampler, SHADER_READ_ONLY layout)
	void writeImageDescriptor(Context& context, VkDescriptorSet set, uint32_t binding, VkImageView view);

	// Starts rendering to every pixel of `target` (its old contents are discarded) and sets viewport and scissor.
	// The target's barrier waits for earlier fragment shader reads: a ping-pong image may have been
	// sampled by the pass before.
	void beginFullscreenRendering(VkCommandBuffer commandBuffer, const Image& target);
} // namespace ReaShader::gpu
