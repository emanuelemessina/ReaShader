/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "render/context.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReaShader::gpu
{
	// Push constants every shader gets (see kShaderPreamble in shader_pass.cpp)
	struct ShaderInputs
	{
		float resolution[2];
		float time;
		float frameRate;
		int32_t frame;
		float videoParam;
	};

	// One slider: a float (or a vector component) in the shader's Params block
	struct ShaderParamField
	{
		std::string name;
		uint32_t offset; // bytes, in the Params block
	};

	// A user fragment shader compiled to SPIR-V, with its Params block reflected
	struct CompiledShader
	{
		std::vector<uint32_t> spirv;
		std::vector<ShaderParamField> params;
		uint32_t paramsSize = 0; // bytes
	};

	// Compiles GLSL written against the ReaShader shader contract.
	// Throws std::runtime_error with the compiler's messages (line numbers match `source`).
	CompiledShader compileShader(const std::string& source, const std::string& name);

	// Runs a compiled shader over the whole frame: samples the input image, renders to the output image
	class ShaderPass
	{
	  public:
		void create(Context& context, const CompiledShader& shader);
		void destroy(Context& context);

		// the image sampled as iChannel0 (rebind after the frame targets are recreated)
		void bindInput(Context& context, VkImageView input);

		// `count` values in [0, 1], in the order of CompiledShader::params
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
