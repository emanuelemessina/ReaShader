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
	namespace
	{
		// built by glslc from src/shaders/internal/fullscreen.vert
		constexpr uint32_t kFullscreenVertexSpirv[] = {
#include "fullscreen.vert.inc"
		};
	} // namespace

	// -------- ShaderPass --------

	void ShaderPass::create(Context& context, const CompiledShader& shader)
	{
		VkDevice device = context.device;
		params = shader.params;

		// descriptors: binding 0 = iChannel0, binding 1 = Params (always present, even if the shader has none)
		VkDescriptorSetLayoutBinding bindings[] = {
			{ kInputBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ kParamsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = 2;
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
		pipelineDesc.vertexSpirv = kFullscreenVertexSpirv;
		pipelineDesc.vertexWords = std::size(kFullscreenVertexSpirv);
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
		VkDescriptorImageInfo imageInfo{ context.sampler, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = descriptorSet;
		write.dstBinding = kInputBinding;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
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
		transition(commandBuffer, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

		VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = target.view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // every pixel gets written
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.renderArea = { { 0, 0 }, target.extent };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		VkViewport viewport{ 0, 0, (float)target.extent.width, (float)target.extent.height, 0, 1 };
		VkRect2D scissor{ { 0, 0 }, target.extent };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet,
								0, nullptr);
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderInputs),
						   &inputs);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);

		vkCmdEndRendering(commandBuffer);
	}
} // namespace ReaShader::gpu
