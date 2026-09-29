/**
 * @file
 * @brief Vulkan helpers: buffers, images, pipelines, barriers; the VMA implementation.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#define VMA_IMPLEMENTATION
#include "render/gpu.h"

namespace ReaShader::gpu
{
	// -------- Buffer --------

	void Buffer::create(VmaAllocator allocator, VkDeviceSize bufferSize, VkBufferUsageFlags usage,
						VmaAllocationCreateFlags hostAccess)
	{
		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = bufferSize;
		bufferInfo.usage = usage;

		VmaAllocationCreateInfo allocationInfo{};
		allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocationInfo.flags = hostAccess | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VmaAllocationInfo result{};
		VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &result));
		mapped = result.pMappedData;
		size = bufferSize;
	}

	void Buffer::destroy(VmaAllocator allocator)
	{
		if (buffer)
			vmaDestroyBuffer(allocator, buffer, allocation);
		*this = {};
	}

	void Buffer::flush(VmaAllocator allocator) const
	{
		vmaFlushAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
	}

	void Buffer::invalidate(VmaAllocator allocator) const
	{
		vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
	}

	// -------- Image --------

	void Image::create(VkDevice device, VmaAllocator allocator, VkExtent2D imageExtent, VkFormat format,
					   VkImageUsageFlags usage, VkImageAspectFlags aspect)
	{
		_create(device, allocator, VK_IMAGE_TYPE_2D, { imageExtent.width, imageExtent.height, 1 }, format, usage, aspect);
	}

	void Image::create3D(VkDevice device, VmaAllocator allocator, uint32_t size, VkFormat format, VkImageUsageFlags usage)
	{
		_create(device, allocator, VK_IMAGE_TYPE_3D, { size, size, size }, format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	void Image::_create(VkDevice device, VmaAllocator allocator, VkImageType type, VkExtent3D imageExtent,
						VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
	{
		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = type;
		imageInfo.format = format;
		imageInfo.extent = imageExtent;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = usage;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocationInfo{};
		allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VK_CHECK(vmaCreateImage(allocator, &imageInfo, &allocationInfo, &image, &allocation, nullptr));

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = image;
		viewInfo.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = { aspect, 0, 1, 0, 1 };
		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &view));

		extent = { imageExtent.width, imageExtent.height };
	}

	void Image::destroy(VkDevice device, VmaAllocator allocator)
	{
		if (view)
			vkDestroyImageView(device, view, nullptr);
		if (image)
			vmaDestroyImage(allocator, image, allocation);
		*this = {};
	}

	// -------- pipelines --------

	namespace
	{
		VkShaderModule createShaderModule(VkDevice device, const uint32_t* spirv, size_t words)
		{
			VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			moduleInfo.codeSize = words * sizeof(uint32_t);
			moduleInfo.pCode = spirv;
			VkShaderModule module = VK_NULL_HANDLE;
			VK_CHECK(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));
			return module;
		}
	} // namespace

	VkPipeline createPipeline(VkDevice device, const PipelineDesc& desc)
	{
		VkShaderModule vertexModule = createShaderModule(device, desc.vertexSpirv, desc.vertexWords);
		VkShaderModule fragmentModule = createShaderModule(device, desc.fragmentSpirv, desc.fragmentWords);

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertexModule;
		stages[0].pName = "main";
		stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragmentModule;
		stages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo noVertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{
			VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
		};
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
		};
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_NONE;
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo depthStencil{
			VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
		};
		bool depthTest = desc.depthFormat != VK_FORMAT_UNDEFINED;
		depthStencil.depthTestEnable = depthTest;
		depthStencil.depthWriteEnable = depthTest;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState blendAttachment{};
		blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
										 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAttachment;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamic.dynamicStateCount = 2;
		dynamic.pDynamicStates = dynamicStates;

		VkFormat colorFormat = kFrameFormat;
		VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachmentFormats = &colorFormat;
		rendering.depthAttachmentFormat = desc.depthFormat;

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &rendering;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = desc.vertexInput ? desc.vertexInput : &noVertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &blend;
		pipelineInfo.pDynamicState = &dynamic;
		pipelineInfo.layout = desc.layout;

		VkPipeline pipeline = VK_NULL_HANDLE;
		VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
		vkDestroyShaderModule(device, fragmentModule, nullptr);
		vkDestroyShaderModule(device, vertexModule, nullptr);
		VK_CHECK(result);
		return pipeline;
	}

	// -------- barriers --------

	void transition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from, VkImageLayout to,
					VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
					VkAccessFlags2 dstAccess, VkImageAspectFlags aspect)
	{
		VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = srcStage;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask = dstStage;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = { aspect, 0, 1, 0, 1 };

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(commandBuffer, &dependency);
	}
} // namespace ReaShader::gpu
