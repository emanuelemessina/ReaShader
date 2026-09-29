/**
 * @file
 * @brief gpu::Lut (a LUT as a 3D image) and gpu::LutPass (applies one to the frame).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"
#include "render/lut_file.h"
#include "render/pass.h"

namespace ReaShader::gpu
{
	// Size of the identity LUT a shader gets as iChannel1 when the LUT isn't the shader's: big enough that the
	// hardware's interpolation weights (8-bit on some GPUs) stay well below one 8-bit step of the output
	constexpr uint32_t kIdentityLutSize = 17;

	// A LUT on the GPU: a size³ RGBA16F image (alpha unused), sampled with the shared linear sampler,
	// so the hardware interpolates between entries
	struct Lut
	{
		// uploads through the context's command buffer: not while a frame is being recorded
		void create(Context& context, const LutData& data);
		void destroy(Context& context);

		Image image;
	};

	// The frame through a LUT: out = mix(in, lut(in), amount); alpha untouched
	class LutPass : public Pass
	{
	  public:
		void create(Context& context);
		void destroy(Context& context);

		void bindInput(Context& context, VkImageView input) override;
		void bindLut(Context& context, VkImageView lut); // bound every frame like the input
		void setAmount(float mix);						 // LUT Mix, 0..1

		void record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs& inputs) override;

	  private:
		float amount = 1.0f;

		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};
} // namespace ReaShader::gpu
