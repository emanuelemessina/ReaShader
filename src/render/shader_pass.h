/**
 * @file
 * @brief gpu::ShaderPass: a compiled shader as a fullscreen pipeline.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"
#include "render/shader_compiler.h"

#include <vector>

namespace ReaShader::gpu
{
	// Runs a compiled shader over the whole frame: samples the input image, renders to the output image
	class ShaderPass
	{
	  public:
		void create(Context& context, const CompiledShader& shader);
		void destroy(Context& context);

		// the image sampled as iChannel0 (rebind after the frame targets are recreated)
		void bindInput(Context& context, VkImageView input);

		// values in the order of CompiledShader::params; params past `count` get their default
		void writeParams(Context& context, const float* values, size_t count);

		void record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs& inputs);

	  private:
		std::vector<ShaderParamField> params;

		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		Buffer paramsBuffer;
	};
} // namespace ReaShader::gpu
