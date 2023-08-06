#include "vktimages.h"
#include "vktcommandpool.h"
#include "vktcommands.h"

namespace vkt
{

	namespace Images
	{
		// AllocatedImage

		AllocatedImage::AllocatedImage(Logical::Device* vktDevice, bool pushToDeviceDeletionQueue)
			: pushToDeletionQueue(pushToDeviceDeletionQueue), vktDevice(vktDevice)
		{
			if (pushToDeviceDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() { destroy(); });
		}
		AllocatedImage::AllocatedImage(Logical::Device* vktDevice, deletion_queue* pCustomDeletionQueue)
			: vktDevice(vktDevice)
		{
			pCustomDeletionQueue->push_function([=]() { destroy(); });
		}

		void AllocatedImage::createImage(VkExtent2D extent, VkImageType type, VkFormat format,
										 VkImageTiling imageTiling, VkImageUsageFlags usageFlags,
										 VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags memoryProperties,
										 VmaAllocationCreateFlags vmaFlags)
		{

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

			uint32_t uniqueQueueFamilies[1] = { vktDevice->physicalDevice->queueFamilyIndices.graphicsFamily.value() };

			createInfo.queueFamilyIndexCount = 1;
			createInfo.pQueueFamilyIndices = uniqueQueueFamilies;

			createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			createInfo.usage = usageFlags;

			VmaAllocationCreateInfo alloccinfo = {};
			alloccinfo.usage = memoryUsage;
			alloccinfo.requiredFlags = VkMemoryPropertyFlags(memoryProperties);
			alloccinfo.flags = vmaFlags;

			VK_CHECK_RESULT(vmaCreateImage(vktDevice->vmaAllocator, &createInfo, &alloccinfo, &image, &allocation,
										   &allocationInfo));
		}

		bool AllocatedImage::createImage(const std::string& filePath, VkAccessFlags finalAccessMask,
										 VkImageLayout finalImageLayout, VkPipelineStageFlags finalStageMask)
		{

			// load image pixels from file

			int texWidth, texHeight, texChannels;

			const char* file = tools::strings::s2cptr(filePath);

			stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

			if (!pixels)
			{
				std::cout << "Failed to load texture file " << file << std::endl;
				return false;
			}

			VkExtent2D extent{ (unsigned)texWidth, (unsigned)texHeight };

			// create dest image

			createImage(extent, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, NULL,
						NULL);

			// layout transition to dst optimal

			VkCommandBuffer cmd = vktDevice->getGraphicsCommandPool()->createCommandBuffer();

			Buffers::AllocatedBuffer* srcBuff = commands::createPixelBufferSrc(vktDevice, (char*)pixels, extent);

			commands::transferPixelBufferToImage(cmd, srcBuff, image, extent, finalAccessMask, finalImageLayout,
												 finalStageMask);

			// execute

			VkFence fence = sync::createFence(vktDevice, false, false);

			vktDevice->getGraphicsCommandPool()->submit(cmd, fence, VK_NULL_HANDLE, VK_NULL_HANDLE);

			vkWaitForFences(vktDevice->vk(), 1, &fence, VK_TRUE, UINT64_MAX);

			// cleanup

			srcBuff->destroy();
			sync::destroySyncObject(vktDevice, fence);
			vkFreeCommandBuffers(vktDevice->vk(), vktDevice->getGraphicsCommandPool()->vk(), 1, &cmd);

			stbi_image_free(pixels);

			return true;
		}

		void AllocatedImage::createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask)
		{
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
			VK_CHECK_RESULT(vkCreateImageView(vktDevice->vk(), &colorImageView, nullptr, &imageView));
		}

		void AllocatedImage::destroy()
		{
			if (imageView)
				vkDestroyImageView(vktDevice->vk(), imageView, nullptr);
			if (image)
			{
				vmaDestroyImage(vktDevice->vmaAllocator, image, nullptr);
			}
			vmaFreeMemory(vktDevice->vmaAllocator, allocation);
			vmaFlushAllocation(vktDevice->vmaAllocator, allocation, 0, VK_WHOLE_SIZE);
		}

	} // namespace Images

} // namespace vkt
