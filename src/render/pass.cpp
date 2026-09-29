/**
 * @file
 * @brief What every fullscreen pass shares: the vertex shader, the start of rendering.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/pass.h"

namespace ReaShader::gpu
{
	namespace
	{
		// built by glslc from src/shaders/internal/fullscreen.vert
		constexpr uint32_t kFullscreenVertexSpirv[] = {
#include "fullscreen.vert.inc"
		};
	} // namespace

	std::span<const uint32_t> fullscreenVertexSpirv()
	{
		return kFullscreenVertexSpirv;
	}

	void writeImageDescriptor(Context& context, VkDescriptorSet set, uint32_t binding, VkImageView view)
	{
		VkDescriptorImageInfo imageInfo{ context.sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = set;
		write.dstBinding = binding;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	void beginFullscreenRendering(VkCommandBuffer commandBuffer, const Image& target)
	{
		transition(commandBuffer, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
				   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

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
	}
} // namespace ReaShader::gpu
