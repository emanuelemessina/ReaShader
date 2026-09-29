/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

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
