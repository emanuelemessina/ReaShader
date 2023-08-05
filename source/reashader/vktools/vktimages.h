#pragma once

#include "vktdevices.h"
#include "vktbuffers.h"
#include "vktsync.h"

#include "vk_mem_alloc.h"

#include <stb_image.h>

#include "tools/strings.h"

namespace vkt {

	namespace Images
	{
		class AllocatedImage
		{
		  public:
			/**
			@param pushToDeviceDeletionQueue If true, the destructor is appended to the queue automatically.
			*/
			AllocatedImage(Logical::Device* vktDevice, bool pushToDeviceDeletionQueue = true);

			AllocatedImage(Logical::Device* vktDevice, deletion_queue* pCustomDeletionQueue);

			/**
			Create empty image with full control
			*/
			void createImage(VkExtent2D extent, VkImageType type, VkFormat format, VkImageTiling imageTiling,
							 VkImageUsageFlags usageFlags, VmaMemoryUsage memoryUsage,
							 VkMemoryPropertyFlags memoryProperties, VmaAllocationCreateFlags vmaFlags = NULL);
			/**
			Create image from file
			*/
			bool createImage(const std::string& filePath, VkAccessFlags finalAccessMask, VkImageLayout finalImageLayout,
							 VkPipelineStageFlags finalStageMask);
			/**
			Create image view from the current image
			*/
			void createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask);

			void destroy();

			/**
			If no image was created yet, returns VK_FORMAT_UNDEFINED.
			*/
			VkFormat getFormat()
			{
				return format;
			}
			VkImage getImage()
			{
				return image;
			}
			VkImageView getImageView()
			{
				return imageView;
			}
			VmaAllocationInfo getAllocationInfo()
			{
				return allocationInfo;
			}
			Logical::Device* getVktDevice()
			{
				return vktDevice;
			}

		  private:
			VkFormat format = VK_FORMAT_UNDEFINED;
			Logical::Device* vktDevice = nullptr;
			bool pushToDeletionQueue;

			VkImage image = VK_NULL_HANDLE;
			VkImageView imageView = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;
			VmaAllocationInfo allocationInfo{};
		};
	}

}