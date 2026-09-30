/**
 * @file
 * @brief gpu::ShaderPass: a compiled shader as a fullscreen pipeline.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/shader_pass.h"

#include <cstring>

namespace ReaShader::gpu
{
	void ShaderPass::create(Context& context, const CompiledShader& shader)
	{
		VkDevice device = context.device;
		params = shader.params;

		// descriptors: iChannel0, Params, iChannel1 (always present, even if the shader doesn't use them)
		VkDescriptorSetLayoutBinding bindings[] = {
			{ kInputBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ kParamsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ kLutBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = (uint32_t)std::size(bindings);
		setLayoutInfo.pBindings = bindings;
		VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout));

		VkPushConstantRange pushConstants{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderInputs) };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &setLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstants;
		VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		setInfo.descriptorPool = context.descriptorPool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &setLayout;
		VK_CHECK(vkAllocateDescriptorSets(device, &setInfo, &descriptorSet));

		// Params values, rewritten every frame
		paramsBuffer.create(context.allocator, shader.paramsSize > 16 ? shader.paramsSize : 16,
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		std::memset(paramsBuffer.mapped, 0, (size_t)paramsBuffer.size);

		VkDescriptorBufferInfo bufferInfo{ paramsBuffer.buffer, 0, VK_WHOLE_SIZE };
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = descriptorSet;
		write.dstBinding = kParamsBinding;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo = &bufferInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		// pipeline: fullscreen triangle, renders straight to the frame format
		PipelineDesc pipelineDesc;
		pipelineDesc.vertexSpirv = fullscreenVertexSpirv().data();
		pipelineDesc.vertexWords = fullscreenVertexSpirv().size();
		pipelineDesc.fragmentSpirv = shader.spirv.data();
		pipelineDesc.fragmentWords = shader.spirv.size();
		pipelineDesc.layout = pipelineLayout;
		pipeline = createPipeline(device, pipelineDesc);
	}

	void ShaderPass::destroy(Context& context)
	{
		VkDevice device = context.device;
		if (pipeline)
			vkDestroyPipeline(device, pipeline, nullptr);
		if (pipelineLayout)
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		if (descriptorSet)
			vkFreeDescriptorSets(device, context.descriptorPool, 1, &descriptorSet);
		if (setLayout)
			vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		paramsBuffer.destroy(context.allocator);
		*this = {};
	}

	void ShaderPass::bindInput(Context& context, VkImageView input)
	{
		writeImageDescriptor(context, descriptorSet, kInputBinding, input);
	}

	void ShaderPass::bindLut(Context& context, VkImageView lut)
	{
		writeImageDescriptor(context, descriptorSet, kLutBinding, lut);
	}

	void ShaderPass::writeParams(Context& context, const float* values, size_t count)
	{
		auto* block = static_cast<uint8_t*>(paramsBuffer.mapped);
		for (size_t i = 0; i < params.size(); i++)
		{
			float value = i < count ? values[i] : params[i].defaultValue;
			std::memcpy(block + params[i].offset, &value, sizeof(float));
		}
		paramsBuffer.flush(context.allocator);
	}

	void ShaderPass::record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs& inputs)
	{
		beginFullscreenRendering(commandBuffer, target);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet,
								0, nullptr);
		ShaderInputs pushed = inputs;
		pushed.frame = frame++;
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderInputs),
						   &pushed);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);

		vkCmdEndRendering(commandBuffer);
	}
} // namespace ReaShader::gpu
