#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

#include <optional>
#include <fstream>
#include <set>

#include <assert.h>
#include <array>
#include <deque>
#include <functional>
#include <any>

#include "vk_mem_alloc.h"

#include "glm.hpp"
#include "gtx/transform.hpp"

using namespace std;

#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::cout << "Fatal : \"" << res << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
		throw std::runtime_error("exception!");																		\
	}																									\
}


namespace vkt {

	struct vktInitProperties {
		bool supportsBlit = false;
	};

	struct vktDeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function) {
			deletors.push_back(function);
		}

		void flush() {
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
				(*it)(); //call the function
			}

			deletors.clear();
		}
	};

	VkInstance createVkInstance(vktDeletionQueue& deletionQueue, char* applicationName, char* engineName);

	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> transferFamily;
		std::optional<uint32_t> computeFamily;
	};

	std::vector<VkPhysicalDevice> enumeratePhysicalDevices(VkInstance& instance);
	/**
	Pass a boolean callback to tell if the device is suitable, the first suitable is returned.
	@param instance
	@param isDeviceSuitable bool callback
	*/
	VkPhysicalDevice pickSuitablePhysicalDevice(VkInstance& instance, bool (*isDeviceSuitable)(VkPhysicalDevice));

	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

	/**
Instantiate a helper object with all the info about the physicalDevice specified, so you don't have to query for properties each time, they're already here upon construction.
*/
	class vktPhysicalDevice {
	public:

		vktPhysicalDevice(vktDeletionQueue& deletionQueue, VkInstance instance, VkPhysicalDevice physicalDevice) {

			this->physicalDevice = physicalDevice;
			this->instance = instance;

			queueFamilyIndices = findQueueFamilies(physicalDevice);

			vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
			vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

			// Memory properties are used regularly for creating all kinds of buffers
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

			// Get list of supported extensions
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
			if (extCount > 0)
			{
				std::vector<VkExtensionProperties> extensions(extCount);
				if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
				{
					for (auto ext : extensions)
					{
						supportedExtensions.push_back(ext.extensionName);
					}
				}
			}

			deletionQueue.push_function([=]() { delete(this); });
		}
		~vktPhysicalDevice() {}

		VkInstance instance;

		VkPhysicalDevice physicalDevice;

		QueueFamilyIndices queueFamilyIndices;

		VkPhysicalDeviceProperties deviceProperties;
		VkPhysicalDeviceFeatures deviceFeatures;

		/** @brief Memory types and heaps of the physical device */
		VkPhysicalDeviceMemoryProperties memoryProperties;

		/** @brief List of extensions supported by the device */
		std::vector<std::string> supportedExtensions;

		// -----

		/**
* Get the index of a memory type that has all the requested property bits set
*
* @param typeBits Bit mask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
* @param properties Bit mask of properties for the memory type to request
* @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
*
* @return Index of the requested memory type
*
* @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
*/
		uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound = nullptr)
		{
			for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
			{
				if ((typeBits & 1) == 1)
				{
					if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
					{
						if (memTypeFound)
						{
							*memTypeFound = true;
						}
						return i;
					}
				}
				typeBits >>= 1;
			}

			if (memTypeFound)
			{
				*memTypeFound = false;
				return 0;
			}
			else
			{
				throw std::runtime_error("Could not find a matching memory type");
			}
		}

		/**
		Returns a VkDevice (logical device) created from this physical device.
		*/
		VkDevice createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures = {}, std::vector<const char*> enabledExtensions = {}, bool useSwapChain = false);

		/**
		* Check if an extension is supported by the (physical device)
		*
		* @param extension Name of the extension to check
		*
		* @return True if the extension is supported (present in the list read at device creation time)
		*/
		bool extensionSupported(std::string extension)
		{
			return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end());
		}

		VkFormatProperties getFormatProperties(VkFormat format) {
			VkFormatProperties formatProps;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
			return formatProps;
		}

	};

	struct AllocatedBuffer {
		VkBuffer buffer;
		VmaAllocation allocation;
	};

	struct VertexInputDescription {

		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;

		VkPipelineVertexInputStateCreateFlags flags = 0;
	};

	struct Vertex {

		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;

		static VertexInputDescription get_vertex_description();
	};

	struct Mesh {
		std::vector<Vertex> vertices;

		AllocatedBuffer vertexBuffer;

		bool load_from_obj(const char* filename);
	};

	/**
	Instantiate a logical VkDevice from the vktPhysicalDevice and options provided.
	Pull a graphics queue and create a graphics command pool upon construction.
	*/
	class vktDevice {
	public:
		vktDevice(
			vktDeletionQueue& deletionQueue,
			vktPhysicalDevice* vktPhysicalDevice,
			VkPhysicalDeviceFeatures enabledFeatures = {},
			std::vector<const char*> enabledExtensions = {},
			bool useSwapChain = false
		) {
			this->device = vktPhysicalDevice->createLogicalDevice(enabledFeatures, enabledExtensions, useSwapChain);
			this->vktPhysicalDevice = vktPhysicalDevice;
			this->pDeletionQueue = &deletionQueue;

			// Get a graphics queue from the device
			vkGetDeviceQueue(device, vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
			// get transfer queue
			vkGetDeviceQueue(device, vktPhysicalDevice->queueFamilyIndices.transferFamily.value(), 0, &transferQueue);

			// create a command pool in the graphics queue
			graphicsCommandPool = createCommandPool(vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value());
			// create transfer command pool
			transferCommandPool = createCommandPool(vktPhysicalDevice->queueFamilyIndices.transferFamily.value());

			// create memory allocator
			VmaAllocatorCreateInfo allocatorInfo = {};
			allocatorInfo.physicalDevice = vktPhysicalDevice->physicalDevice;
			allocatorInfo.device = device;
			allocatorInfo.instance = vktPhysicalDevice->instance;
			vmaCreateAllocator(&allocatorInfo, &vmaAllocator);

			deletionQueue.push_function([=]() { delete(this); });
		}

		~vktDevice() {
			vmaDestroyAllocator(vmaAllocator);
			vkDestroyDevice(device, nullptr);
		}

		vktPhysicalDevice* vktPhysicalDevice;
		VkDevice device;

		vktDeletionQueue* pDeletionQueue;

		VkQueue graphicsQueue = VK_NULL_HANDLE;
		VkQueue transferQueue = VK_NULL_HANDLE;

		VkCommandPool graphicsCommandPool = VK_NULL_HANDLE;
		VkCommandPool transferCommandPool = VK_NULL_HANDLE;

		VmaAllocator vmaAllocator = VK_NULL_HANDLE;

		// create pipeline objects
		VkShaderModule createShaderModule(char* spvPath);
		VkCommandPool createCommandPool(uint32_t queueFamilyIndex);
		VkCommandBuffer vktDevice::createCommandBuffer(VkCommandPool dedicatedCommandPool = VK_NULL_HANDLE);
		void vktDevice::restartCommandBuffer(VkCommandBuffer& previousCommandBuffer);

		// wait and submit
		void waitIdle() {
			vkDeviceWaitIdle(device);
		}
		void submitQueue(VkCommandBuffer commandBuffer, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore, VkQueue queue = VK_NULL_HANDLE);

		// sync objects
		VkFence createFence(bool signaled) {
			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			if (signaled)
				fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
			return fence;
		}
		VkFence createFence(bool signaled, vktDeletionQueue& deletionQueue) {
			VkFence fence = createFence(signaled);
			deletionQueue.push_function([=]() {
				destroySyncObjects({ fence });
				});
			return fence;
		}

		VkSemaphore createSemaphore() {
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkSemaphore semaphore;
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
			return semaphore;
		}
		VkSemaphore createSemaphore(vktDeletionQueue& deletionQueue) {
			VkSemaphore semaphore = createSemaphore();
			deletionQueue.push_function([=]() {
				destroySyncObjects({ semaphore });
				});
			return semaphore;
		}

		void destroySyncObjects(std::vector<std::any> objs) {
			for (auto obj : objs)
			{
				if (obj.type() == typeid(VkFence))
					vkDestroyFence(device, std::any_cast<VkFence>(obj), nullptr);
				else if (obj.type() == typeid(VkSemaphore))
					vkDestroySemaphore(device, std::any_cast<VkSemaphore>(obj), nullptr);
			}
		}

		// allocated buffers
		void allocateMesh(vkt::Mesh* mesh) {
			//allocate vertex buffer
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			//this is the total size, in bytes, of the buffer we are allocating
			bufferInfo.size = mesh->vertices.size() * sizeof(vkt::Vertex);
			//this buffer is going to be used as a Vertex Buffer
			bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

			VmaAllocationCreateInfo vmaallocInfo = {};
			vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

			//allocate the buffer
			VK_CHECK_RESULT(vmaCreateBuffer(vmaAllocator, &bufferInfo, &vmaallocInfo,
				&(mesh->vertexBuffer.buffer),
				&(mesh->vertexBuffer.allocation),
				nullptr));

			//copy vertex data
			void* data;
			vmaMapMemory(vmaAllocator, mesh->vertexBuffer.allocation, &data);

			memcpy(data, mesh->vertices.data(), mesh->vertices.size() * sizeof(vkt::Vertex));

			vmaUnmapMemory(vmaAllocator, mesh->vertexBuffer.allocation);

			pDeletionQueue->push_function([=]() {
				vmaDestroyBuffer(vmaAllocator, mesh->vertexBuffer.buffer, mesh->vertexBuffer.allocation);
				});
		}

	};

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