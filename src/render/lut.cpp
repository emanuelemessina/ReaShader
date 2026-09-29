/**
 * @file
 * @brief gpu::Lut and gpu::LutPass.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/lut.h"

#include <glm/gtc/packing.hpp>

namespace ReaShader::gpu
{
	namespace
	{
		// built by glslc from src/shaders/internal/lut.frag
		constexpr uint32_t kLutFragmentSpirv[] = {
#include "lut.frag.inc"
		};

		// guaranteed sampleable with linear filtering (3-channel formats aren't)
		constexpr VkFormat kLutFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

		constexpr uint32_t kFrameBinding = 0;
		constexpr uint32_t kTableBinding = 1;
	} // namespace

	// -------- Lut --------

	void Lut::create(Context& context, const LutData& data)
	{
		// RGB floats -> RGBA halves
		size_t entries = size_t(data.size) * data.size * data.size;
		Buffer staging;
		staging.create(context.allocator, entries * 4 * sizeof(uint16_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		auto* texels = static_cast<uint16_t*>(staging.mapped);
		for (size_t i = 0; i < entries; i++)
		{
			texels[i * 4 + 0] = glm::packHalf1x16(data.rgb[i * 3 + 0]);
			texels[i * 4 + 1] = glm::packHalf1x16(data.rgb[i * 3 + 1]);
			texels[i * 4 + 2] = glm::packHalf1x16(data.rgb[i * 3 + 2]);
			texels[i * 4 + 3] = glm::packHalf1x16(1.0f);
		}
		staging.flush(context.allocator);

		try
		{
			image.create3D(context.device, context.allocator, data.size, kLutFormat,
						   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

			VkCommandBuffer commandBuffer = context.beginCommands();
			transition(commandBuffer, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
					   VK_ACCESS_2_TRANSFER_WRITE_BIT);
			VkBufferImageCopy region{};
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageExtent = { data.size, data.size, data.size };
			vkCmdCopyBufferToImage(commandBuffer, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
								   &region);
			transition(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
					   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					   VK_ACCESS_2_SHADER_READ_BIT);
			context.submitAndWait();
		}
		catch (...)
		{
			staging.destroy(context.allocator);
			throw;
		}
		staging.destroy(context.allocator);
	}

	void Lut::destroy(Context& context)
	{
		image.destroy(context.device, context.allocator);
	}

	// -------- LutPass --------

	void LutPass::create(Context& context)
	{
		VkDevice device = context.device;

		VkDescriptorSetLayoutBinding bindings[] = {
			{ kFrameBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ kTableBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = (uint32_t)std::size(bindings);
		setLayoutInfo.pBindings = bindings;
		VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout));

		VkPushConstantRange pushConstants{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) };
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

		PipelineDesc pipelineDesc;
		pipelineDesc.vertexSpirv = fullscreenVertexSpirv().data();
		pipelineDesc.vertexWords = fullscreenVertexSpirv().size();
		pipelineDesc.fragmentSpirv = kLutFragmentSpirv;
		pipelineDesc.fragmentWords = std::size(kLutFragmentSpirv);
		pipelineDesc.layout = pipelineLayout;
		pipeline = createPipeline(device, pipelineDesc);
	}

	void LutPass::destroy(Context& context)
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
		*this = {};
	}

	void LutPass::bindInput(Context& context, VkImageView input)
	{
		writeImageDescriptor(context, descriptorSet, kFrameBinding, input);
	}

	void LutPass::bindLut(Context& context, VkImageView lut)
	{
		writeImageDescriptor(context, descriptorSet, kTableBinding, lut);
	}

	void LutPass::setAmount(float mix)
	{
		amount = mix;
	}

	void LutPass::record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs&)
	{
		beginFullscreenRendering(commandBuffer, target);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet,
								0, nullptr);
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &amount);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);

		vkCmdEndRendering(commandBuffer);
	}
} // namespace ReaShader::gpu
