#pragma once

#include "vktbuffers.h"

namespace vkt
{
	/**
	 * images commands
	 */
	namespace commands
	{

		static void insertImageMemoryBarrier(VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags srcAccessMask,
											 VkAccessFlags dstAccessMask, VkImageLayout oldImageLayout,
											 VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask,
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

			vkCmdPipelineBarrier(cmdbuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1,
								 &imageMemoryBarrier);
		}

		/**
		Allocates a buffer with pixelSrc contents for transfer src usage.
		*/
		static Buffers::AllocatedBuffer* createPixelBufferSrc(Logical::Device* vktDevice, char* pixelsSrc,
															  VkExtent2D extent)
		{
			// create staging buffer
			int buffSize = extent.width * extent.height * 4;
			Buffers::AllocatedBuffer* srcBuff = new Buffers::AllocatedBuffer(vktDevice, false);
			srcBuff->allocate(buffSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

			// copy pixels to staging buffer

			void* data;
			VK_CHECK_RESULT(vmaMapMemory(vktDevice->vmaAllocator, srcBuff->getAllocation(), &data))

			memcpy(data, pixelsSrc, buffSize);

			vmaUnmapMemory(vktDevice->vmaAllocator, srcBuff->getAllocation());

			return srcBuff;
		}

		/**
		 * Transfer allocated pixel buffer (usage transfer src) to image.
		 *
		 * \param cmd
		 * \param srcBuff
		 * \param imageDst
		 * \param extent
		 * \param finalAccessMask
		 * \param finalImageLayout
		 * \param finalStageMask
		 */
		static void transferPixelBufferToImage(VkCommandBuffer cmd, Buffers::AllocatedBuffer* srcBuff, VkImage imageDst,
											   VkExtent2D extent, VkAccessFlags finalAccessMask,
											   VkImageLayout finalImageLayout, VkPipelineStageFlags finalStageMask)
		{

			// layout transition to dst optimal

			commands::insertImageMemoryBarrier(cmd, imageDst, 0, VK_ACCESS_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
											   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
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

			vkCmdCopyBufferToImage(cmd, srcBuff->getBuffer(), imageDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
								   &copyRegion);

			// retransition

			commands::insertImageMemoryBarrier(cmd, imageDst, 0, finalAccessMask, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											   finalImageLayout, VK_PIPELINE_STAGE_TRANSFER_BIT, finalStageMask,
											   VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		}

		/**
		 * Transfer raw buffer to allocated image (currently the image must be mapped).
		 */
		static void transferRawBufferToImage(Logical::Device* vktDevice, VkCommandBuffer cmd, void* srcBuffer,
											 Images::AllocatedImage* imageDst, size_t size)
		{
			// transition to general (as transfer reciever)
			commands::insertImageMemoryBarrier(cmd, imageDst->getImage(), 0, VK_ACCESS_MEMORY_WRITE_BIT,
											   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
											   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
											   VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

			// Get layout of the image (including row pitch)
			VkImageSubresource subResource{};
			subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			VkSubresourceLayout subResourceLayout;
			vkGetImageSubresourceLayout(vktDevice->vk(), imageDst->getImage(), &subResource, &subResourceLayout);

			// copy bits from src to framedest
			memcpy((void*)(reinterpret_cast<uintptr_t>((imageDst->getAllocationInfo()).pMappedData) +
						   subResourceLayout.offset),
				   (void*)srcBuffer, size);
		}
	}; // namespace commands
} // namespace vkt
