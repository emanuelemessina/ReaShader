/**
 * @file
 * @brief gpu::ShaderPass: a compiled shader as a fullscreen pipeline.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"
#include "render/pass.h"
#include "render/shader_compiler.h"

#include <vector>

namespace ReaShader::gpu
{
	// Runs a compiled shader over the whole frame: samples its input as iChannel0, renders to the target
	class ShaderPass : public Pass
	{
	  public:
		void create(Context& context, const CompiledShader& shader);
		void destroy(Context& context);

		void bindInput(Context& context, VkImageView input) override; // iChannel0
		void bindLut(Context& context, VkImageView lut);			  // iChannel1, bound every frame like the input

		// values in the order of CompiledShader::params; params past `count` get their default
		void writeParams(Context& context, const float* values, size_t count);

		void record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs& inputs) override;

	  private:
		std::vector<ShaderParamField> params;

		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		Buffer paramsBuffer;
	};
} // namespace ReaShader::gpu
