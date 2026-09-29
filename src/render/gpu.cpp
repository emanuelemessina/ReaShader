/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

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
					   VkImageUsageFlags usage)
	{
		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { imageExtent.width, imageExtent.height, 1 };
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
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &view));

		extent = imageExtent;
	}

	void Image::destroy(VkDevice device, VmaAllocator allocator)
	{
		if (view)
			vkDestroyImageView(device, view, nullptr);
		if (image)
			vmaDestroyImage(allocator, image, allocation);
		*this = {};
	}

	// -------- barriers --------

	void transition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from, VkImageLayout to,
					VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
					VkAccessFlags2 dstAccess)
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
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(commandBuffer, &dependency);
	}
} // namespace ReaShader::gpu
