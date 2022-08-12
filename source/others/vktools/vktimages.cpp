//#include "vktimages.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "vktrendering.h"

namespace vkt {

	// CREATE ATTACHMENT IMAGE

	/**
	Creates image and binds it to device memory
	*/
	void vktAttachment::createImage(int w, int h, VkImageType type, VkFormat format, VkImageTiling imageTiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memoryProperties) {

		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

		createInfo.imageType = type;
		createInfo.format = format;

		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;

		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.tiling = imageTiling; // VK_IMAGE_TILING_LINEAR

		//createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		createInfo.extent.width = w;
		createInfo.extent.height = h;
		createInfo.extent.depth = 1;

		uint32_t uniqueQueueFamilies[1] = { vktDevice->vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value() };

		createInfo.queueFamilyIndexCount = 1;
		createInfo.pQueueFamilyIndices = uniqueQueueFamilies;

		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.usage = usageFlags;

		if (vkCreateImage(vktDevice->device, &createInfo, nullptr, &image) != VK_SUCCESS) {
			throw std::runtime_error("failed to create image!");
		}

		bindToMemory(memoryProperties);
	}
	void vktAttachment::bindToMemory(VkMemoryPropertyFlags memoryProperties) {
		// bind to memory

		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(vktDevice->device, image, &memReqs);

		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vktDevice->vktPhysicalDevice->getMemoryType(memReqs.memoryTypeBits, memoryProperties);

		VK_CHECK_RESULT(vkAllocateMemory(vktDevice->device, &memAlloc, nullptr, &memory));
		VK_CHECK_RESULT(vkBindImageMemory(vktDevice->device, image, memory, 0));
	}
	void vktAttachment::createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask) {
		VkImageViewCreateInfo colorImageView{};
		colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorImageView.viewType = type;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = aspectMask;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = image;
		VK_CHECK_RESULT(vkCreateImageView(vktDevice->device, &colorImageView, nullptr, &imageView));
	}

	// ALLOCATED IMAGE

	AllocatedImage::~AllocatedImage() {
		if (imageView)
			vkDestroyImageView(vktDevice->device, imageView, nullptr);
		if (image) {
			vmaDestroyImage(vktDevice->vmaAllocator, image, nullptr);
		}
		vmaFreeMemory(vktDevice->vmaAllocator, allocation);
		vmaFlushAllocation(vktDevice->vmaAllocator, allocation, 0, VK_WHOLE_SIZE);
	}
	void AllocatedImage::createImage(VkExtent2D extent,
		VkImageType type, VkFormat format,
		VkImageTiling imageTiling,
		VkImageUsageFlags usageFlags,
		VmaMemoryUsage memoryUsage,
		VkMemoryPropertyFlags memoryProperties,
		VmaAllocationCreateFlags vmaFlags) {

		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

		createInfo.imageType = type;
		createInfo.format = format;

		this->format = format;

		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;

		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.tiling = imageTiling; // VK_IMAGE_TILING_LINEAR

		//createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		createInfo.extent.width = extent.width;
		createInfo.extent.height = extent.height;
		createInfo.extent.depth = 1;

		uint32_t uniqueQueueFamilies[1] = { vktDevice->vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value() };

		createInfo.queueFamilyIndexCount = 1;
		createInfo.pQueueFamilyIndices = uniqueQueueFamilies;

		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.usage = usageFlags;

		VmaAllocationCreateInfo alloccinfo = {};
		alloccinfo.usage = memoryUsage;
		alloccinfo.requiredFlags = VkMemoryPropertyFlags(memoryProperties);
		alloccinfo.flags = vmaFlags;

		VK_CHECK_RESULT(vmaCreateImage(vktDevice->vmaAllocator, &createInfo, &alloccinfo, &image, &allocation, &allocationInfo));
	}
	void AllocatedImage::createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectFlags) {
		VkImageViewCreateInfo colorImageView{};
		colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorImageView.viewType = type;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = aspectFlags;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = image;
		VK_CHECK_RESULT(vkCreateImageView(vktDevice->device, &colorImageView, nullptr, &imageView));
	}
	/**
	Populates image from a file, no need to create image manually.
	*/
	bool AllocatedImage::load_from_file(const char* file, VkAccessFlags finalAccessMask, VkImageLayout finalImageLayout, VkPipelineStageFlags finalStageMask) {

		// load image pixels from file

		int texWidth, texHeight, texChannels;

		stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

		if (!pixels) {
			std::cout << "Failed to load texture file " << file << std::endl;
			return false;
		}

		// put pixels in buffer

		int buffSize = texWidth * texHeight * 4;
		AllocatedBuffer srcBuff;
		srcBuff.createBuffer(vktDevice, buffSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, false);

		void* data;
		vmaMapMemory(vktDevice->vmaAllocator, srcBuff.allocation, &data);

		memcpy(data, pixels, buffSize);

		vmaUnmapMemory(vktDevice->vmaAllocator, srcBuff.allocation);

		stbi_image_free(pixels);

		// create dest image

		createImage(
			{ (unsigned)texWidth, (unsigned)texHeight },
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			NULL, NULL
		);

		// layout transition to dst optimal

		VkCommandBuffer cmd = vktDevice->createCommandBuffer(nullptr, false);

		vkt::insertImageMemoryBarrier(
			cmd,
			image,
			0,
			VK_ACCESS_MEMORY_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// copy buffer to image

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = { (unsigned)texWidth, (unsigned)texHeight, 1 };

		vkCmdCopyBufferToImage(cmd, srcBuff.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		// retransition

		vkt::insertImageMemoryBarrier(
			cmd,
			image,
			0,
			finalAccessMask,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			finalImageLayout,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			finalStageMask,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// execute

		VkFence fence = vktDevice->createFence(false, false);

		vktDevice->submitQueue(cmd, fence, VK_NULL_HANDLE, VK_NULL_HANDLE);

		vkWaitForFences(vktDevice->device, 1, &fence, VK_TRUE, UINT64_MAX);

		// cleanup

		srcBuff.destroy();
		vktDevice->destroySyncObjects({ fence });
		vkFreeCommandBuffers(vktDevice->device, vktDevice->graphicsCommandPool, 1, &cmd);

		return true;
	}

	void insertImageMemoryBarrier(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange)
	{
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.srcAccessMask = srcAccessMask;
		imageMemoryBarrier.dstAccessMask = dstAccessMask;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		vkCmdPipelineBarrier(
			cmdbuffer,
			srcStageMask,
			dstStageMask,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);
	}
}