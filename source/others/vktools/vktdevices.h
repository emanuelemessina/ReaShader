#pragma once

#include "vktools/vktools.h"

namespace vkt {
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
		VkDevice createLogicalDevice(void* pNext = nullptr, VkPhysicalDeviceFeatures enabledFeatures = {}, std::vector<const char*> enabledExtensions = {}, bool useSwapChain = false);

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

		size_t pad_uniform_buffer_size(size_t originalSize);
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
			VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
			shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
			shader_draw_parameters_features.pNext = nullptr;
			shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;

			this->device = vktPhysicalDevice->createLogicalDevice(&shader_draw_parameters_features, enabledFeatures, enabledExtensions, useSwapChain);
			this->vktPhysicalDevice = vktPhysicalDevice;
			this->pDeletionQueue = &deletionQueue;

			deletionQueue.push_function([=]() { delete(this); });

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

			deletionQueue.push_function([=]() { 	vmaDestroyAllocator(vmaAllocator);	});
		}

		~vktDevice() {
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
		VkShaderModule createShaderModule(const char* spvPath);
		VkCommandPool createCommandPool(uint32_t queueFamilyIndex);
		VkCommandBuffer createCommandBuffer(VkCommandPool dedicatedCommandPool = VK_NULL_HANDLE, bool pushToDeletionQueue = true);
		void restartCommandBuffer(VkCommandBuffer& previousCommandBuffer);

		VkDescriptorSetLayout createDescriptorSetLayout(std::vector<VkDescriptorSetLayoutBinding> bindings);
		VkDescriptorPool createDescriptorPool(std::vector<VkDescriptorPoolSize> sizes, int maxSets);
		void allocateDescriptorSet(
			VkDescriptorPool descriptorPool,
			VkDescriptorSetLayout* pDescriptorSetLayout,
			VkDescriptorSet* pDescriptorSet);

		VkSampler createSampler(VkFilter filters, VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT) {
			VkSamplerCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			info.pNext = nullptr;

			info.magFilter = filters;
			info.minFilter = filters;
			info.addressModeU = samplerAddressMode;
			info.addressModeV = samplerAddressMode;
			info.addressModeW = samplerAddressMode;

			VkSampler sampler;

			VK_CHECK_RESULT(vkCreateSampler(device, &info, nullptr, &sampler));

			return sampler;
		}

		// wait and submit
		void waitIdle() {
			vkDeviceWaitIdle(device);
		}
		void submitQueue(VkCommandBuffer commandBuffer, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore, VkQueue queue = VK_NULL_HANDLE);

		// sync objects
		VkFence createFence(bool signaled, bool pushToDeletionQueue = true) {
			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			if (signaled)
				fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
			if (pushToDeletionQueue)
				pDeletionQueue->push_function([=]() {
				destroySyncObjects({ fence });
					});
			return fence;
		}

		VkSemaphore createSemaphore(bool pushToDeletionQueue = true) {
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkSemaphore semaphore;
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
			if (pushToDeletionQueue)
				pDeletionQueue->push_function([=]() {
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

	};
}