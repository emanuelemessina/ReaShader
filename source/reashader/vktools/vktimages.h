#pragma once

#include "vktdevices.h"
#include "vktbuffers.h"
#include "vktsync.h"

#include "vk_mem_alloc.h"

#include <stb_image.h>

#include "tools/strings.h"

namespace vkt {

	/**
	 * images commands
	 */
	namespace commands {

		static void insertImageMemoryBarrier(
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

		/**
		Allocates a buffer with pixelSrc contents for transfer src usage.
		*/
		static vktAllocatedBuffer* createPixelBufferSrc(vktDevice* vktDevice, char* pixelsSrc, VkExtent2D extent) {
			// create staging buffer
			int buffSize = extent.width * extent.height * 4;
			vktAllocatedBuffer* srcBuff = new vktAllocatedBuffer(vktDevice, false);
			srcBuff->allocate(buffSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

			// copy pixels to staging buffer

			void* data;
			vmaMapMemory(vktDevice->vmaAllocator, srcBuff->getAllocation(), &data);

			memcpy(data, pixelsSrc, buffSize);

			vmaUnmapMemory(vktDevice->vmaAllocator, srcBuff->getAllocation());

			return srcBuff;
		}

		static void transferPixelBufferToImage(
			VkCommandBuffer cmd,
			vktAllocatedBuffer* srcBuff,
			VkImage imageDst,
			VkExtent2D extent,
			VkAccessFlags finalAccessMask, VkImageLayout finalImageLayout, VkPipelineStageFlags finalStageMask) {

			// layout transition to dst optimal

			vkt::commands::insertImageMemoryBarrier(
				cmd,
				imageDst,
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
			copyRegion.imageExtent = { extent.width, extent.height, 1 };

			vkCmdCopyBufferToImage(cmd, srcBuff->getBuffer(), imageDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			// retransition

			vkt::commands::insertImageMemoryBarrier(
				cmd,
				imageDst,
				0,
				finalAccessMask,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				finalImageLayout,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				finalStageMask,
				VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		}
	};

	class vktAllocatedImage {
	public:
		/**
		@param pushToDeviceDeletionQueue If true, the destructor is appended to the queue automatically.
		*/
		vktAllocatedImage(vktDevice* vktDevice, bool pushToDeviceDeletionQueue = true)
			: pushToDeletionQueue(pushToDeviceDeletionQueue), vktDevice(vktDevice) {
			if (pushToDeviceDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() {
				destroy();
					});
		}
		vktAllocatedImage(vktDevice* vktDevice, vktDeletionQueue* pCustomDeletionQueue)
			: vktDevice(vktDevice) {
			pCustomDeletionQueue->push_function([=]() {
				destroy();
				});
		}

		/**
		Create empty image with full control
		*/
		void createImage(VkExtent2D extent,
			VkImageType type, VkFormat format,
			VkImageTiling imageTiling,
			VkImageUsageFlags usageFlags,
			VmaMemoryUsage memoryUsage,
			VkMemoryPropertyFlags memoryProperties,
			VmaAllocationCreateFlags vmaFlags = NULL) {

			VkImageCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

			createInfo.imageType = type;
			createInfo.format = format;

			this->format = format;

			createInfo.mipLevels = 1;
			createInfo.arrayLayers = 1;

			createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			createInfo.tiling = imageTiling;

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
		/**
		Create image from file
		*/
		bool createImage(const std::string& filePath, VkAccessFlags finalAccessMask, VkImageLayout finalImageLayout, VkPipelineStageFlags finalStageMask) {

			// load image pixels from file

			int texWidth, texHeight, texChannels;

			const char* file = tools::strings::s2cptr(filePath);

			stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

			if (!pixels) {
				std::cout << "Failed to load texture file " << file << std::endl;
				return false;
			}

			VkExtent2D extent{ (unsigned)texWidth, (unsigned)texHeight };

			// create dest image

			createImage(
				extent,
				VK_IMAGE_TYPE_2D,
				VK_FORMAT_R8G8B8A8_SRGB,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY,
				NULL, NULL
			);

			// layout transition to dst optimal

			VkCommandBuffer cmd = vktDevice->getGraphicsCommandPool()->createCommandBuffer();

			vktAllocatedBuffer* srcBuff = vkt::commands::createPixelBufferSrc(vktDevice, (char*)pixels, extent);

			vkt::commands::transferPixelBufferToImage(
				cmd,
				srcBuff,
				image,
				extent,
				finalAccessMask,
				finalImageLayout,
				finalStageMask
			);

			// execute

			VkFence fence = vkt::syncObjects::createFence(vktDevice, false, false);

			vktDevice->getGraphicsCommandPool()->submit(cmd, fence, VK_NULL_HANDLE, VK_NULL_HANDLE);

			vkWaitForFences(vktDevice->device, 1, &fence, VK_TRUE, UINT64_MAX);

			// cleanup

			srcBuff->destroy();
			vkt::syncObjects::destroySyncObject(vktDevice, fence);
			vkFreeCommandBuffers(vktDevice->device, vktDevice->getGraphicsCommandPool()->get(), 1, &cmd);

			stbi_image_free(pixels);

			return true;
		}
		/**
		Create image view from the current image
		*/
		void createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask) {
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

		void destroy() {
			if (imageView)
				vkDestroyImageView(vktDevice->device, imageView, nullptr);
			if (image) {
				vmaDestroyImage(vktDevice->vmaAllocator, image, nullptr);
			}
			vmaFreeMemory(vktDevice->vmaAllocator, allocation);
			vmaFlushAllocation(vktDevice->vmaAllocator, allocation, 0, VK_WHOLE_SIZE);
		}

		/**
		If no image was created yet, returns VK_FORMAT_UNDEFINED.
		*/
		VkFormat getFormat() { return format; }
		VkImage getImage() { return image; }
		VkImageView getImageView() { return imageView; }
		VmaAllocationInfo getAllocationInfo() { return allocationInfo; }
		vktDevice* getVktDevice() { return vktDevice; }

	private:
		VkFormat format = VK_FORMAT_UNDEFINED;
		vktDevice* vktDevice = nullptr;
		bool pushToDeletionQueue;

		VkImage image = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VmaAllocationInfo allocationInfo{};
	};

}