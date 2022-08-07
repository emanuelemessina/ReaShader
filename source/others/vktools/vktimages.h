#pragma once

#include "vktdevices.h"

#include "vk_mem_alloc.h"

namespace vkt {

	class AllocatedImage {
	public:
		/**
		The destructor is appended to the queue automatically.
		*/
		AllocatedImage(vktDeletionQueue& deletionQueue, vktDevice* vktDevice) {
			this->vktDevice = vktDevice;
			deletionQueue.push_function([=]() {
				delete(this);
				});
		}
		AllocatedImage(vktDevice* vktDevice) {
			this->vktDevice = vktDevice;
		}
		~AllocatedImage();

		vktDevice* vktDevice;
		vktDeletionQueue* pDeletionQueue;

		VkImage image = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VmaAllocation allocation;
		VmaAllocationInfo allocationInfo{};

		void createImage(VkExtent2D extent,
			VkImageType type, VkFormat format,
			VkImageTiling imageTiling,
			VkImageUsageFlags usageFlags,
			VmaMemoryUsage memoryUsage,
			VkMemoryPropertyFlags memoryProperties,
			VmaAllocationCreateFlags vmaFlags = NULL);
		void createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask);

	};

	/**
	DEPRECATED
	Manual allocation of a VkImage. Use AllocatedImage which uses VmaAllocator
	*/
	class vktAttachment {
	public:
		vktAttachment(vktDevice* vktDevice) {
			this->vktDevice = vktDevice;
		}
		~vktAttachment() {
			// dealloc mem
			vkUnmapMemory(vktDevice->device, memory);
			vkFreeMemory(vktDevice->device, memory, nullptr);
			// destroy vk objects
			if (imageView) {
				vkDestroyImageView(vktDevice->device, imageView, nullptr);
			}
			vkDestroyImage(vktDevice->device, image, nullptr);
		}

		void createImage(int w, int h, VkImageType type, VkFormat format, VkImageTiling imageTiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memoryProperties);
		void createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask);

		// -------

		vktDevice* vktDevice;

		VkImage image;
		VkImageView imageView = VK_NULL_HANDLE;
		VkDeviceMemory memory;

	private:
		void bindToMemory(VkMemoryPropertyFlags memoryProperties);
	};

	void insertImageMemoryBarrier(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange);
}