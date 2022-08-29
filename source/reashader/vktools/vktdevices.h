#pragma once

#include "vktools/vktools.h"
#include "vktcommandqueue.h"

namespace vkt {

	class physicalDevices {
	public:

		struct QueueFamilyIndices {
			std::optional<uint32_t> graphicsFamily;
			std::optional<uint32_t> transferFamily;
			std::optional<uint32_t> computeFamily;
		};

		static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physicalDevice) {

			// get families

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

			physicalDevices::QueueFamilyIndices queueFamilyIndices;

			// get num of assignable queues (how many bits are set)

			std::vector<int> queueFamiliesBitMatches(queueFamilyCount);

			for (uint32_t i = 0; i < queueFamilyCount; i++) {

				auto flags = queueFamilies[i].queueFlags;

				int* m = &queueFamiliesBitMatches[i];

				if (flags & VK_QUEUE_GRAPHICS_BIT) (*m)++;
				if (flags & VK_QUEUE_COMPUTE_BIT) (*m)++;
				if (flags & VK_QUEUE_TRANSFER_BIT) (*m)++;

			}

			// sort found queuefamilies based on num matches (ascending)

			std::vector<int> sortedIndices(queueFamilyCount);
			std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
			std::sort(sortedIndices.begin(), sortedIndices.end(),
				[&](int A, int B) -> bool {
					return queueFamiliesBitMatches[A] < queueFamiliesBitMatches[B];
				});

			// loop thru sorted queues
			// fewest matches first -> means that family is more specialized than one with more matches (generic family)
			// once queue is assigned it's not assignable anymore and it's taken by that family
			// sorting ensures that specialized families come first

			bool
				graphicsAssignable = true,
				computeAssignable = true,
				transferAssignable = true;

			for (uint32_t i = 0; i < queueFamilyCount; i++) {

				auto flags = queueFamilies[sortedIndices[i]].queueFlags;

				if (flags & VK_QUEUE_GRAPHICS_BIT && graphicsAssignable) {
					queueFamilyIndices.graphicsFamily = sortedIndices[i];
					graphicsAssignable = false;
				}
				if (flags & VK_QUEUE_COMPUTE_BIT && computeAssignable) {
					queueFamilyIndices.computeFamily = sortedIndices[i];
					computeAssignable = false;
				}
				if (flags & VK_QUEUE_TRANSFER_BIT && transferAssignable) {
					queueFamilyIndices.transferFamily = sortedIndices[i];
					transferAssignable = false;
				}
			}

			return queueFamilyIndices;
		}

		physicalDevices& enumerate(VkInstance instance) {
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

			if (deviceCount == 0) {
				throw std::runtime_error("failed to find GPUs with Vulkan support!");
			}

			m_physicalDevices.resize(deviceCount);

			vkEnumeratePhysicalDevices(instance, &deviceCount, m_physicalDevices.data());

			return *this;
		}

		physicalDevices& removeUnsuitable(bool (*isDeviceSuitable)(VkPhysicalDevice)) {
			m_physicalDevices.erase(
				std::remove_if(
					m_physicalDevices.begin(), m_physicalDevices.end(),
					[&](const VkPhysicalDevice pD) {
						return !isDeviceSuitable(pD);
					}
				), m_physicalDevices.end()
						);

			if (m_physicalDevices.empty()) {
				throw std::runtime_error("failed to find a suitable GPU!");
			}

			return *this;
		}

		std::vector<VkPhysicalDevice> get() {
			return m_physicalDevices;
		}

	private:
		std::vector<VkPhysicalDevice> m_physicalDevices{};
	};

	/**
Instantiate a helper object with all the info about the physicalDevice specified, so you don't have to query for properties each time, they're already here upon construction.
*/
	class vktPhysicalDevice {
	public:

		vktPhysicalDevice(
			vktDeletionQueue& deletionQueue,
			VkInstance instance,
			VkPhysicalDevice physicalDevice)
			: physicalDevice(physicalDevice), instance(instance) {

			queueFamilyIndices = physicalDevices::findQueueFamilies(physicalDevice);

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
					for (const auto& ext : extensions)
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

		physicalDevices::QueueFamilyIndices queueFamilyIndices;

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
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
			return formatProps;
		}

		bool supportsBlit() {

			static std::optional<bool> supportsBlit;

			if (supportsBlit.has_value())
				return supportsBlit.value();

			// Check blit support for source and destination
			supportsBlit = (getFormatProperties(VK_FORMAT_R8G8B8A8_UNORM).linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

			return supportsBlit.value();
		}
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
		) : vktPhysicalDevice(vktPhysicalDevice), pDeletionQueue(&deletionQueue) {

			deletionQueue.push_function([=]() { delete(this); });

			VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
			shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
			shader_draw_parameters_features.pNext = nullptr;
			shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;

			createLogicalDevice(&shader_draw_parameters_features, enabledFeatures, enabledExtensions, useSwapChain);

			graphicsQueue = new vktQueue(deletionQueue, device, vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value());
			transferQueue = new vktQueue(deletionQueue, device, vktPhysicalDevice->queueFamilyIndices.transferFamily.value());

			graphicsCommandPool = new vktCommandPool(deletionQueue, graphicsQueue);
			transferCommandPool = new vktCommandPool(deletionQueue, transferQueue);

			createMemoryAllocator();
		}

		~vktDevice() {
		}

		vktPhysicalDevice* vktPhysicalDevice;
		VkDevice device = VK_NULL_HANDLE;

		vktDeletionQueue* pDeletionQueue;

		VmaAllocator vmaAllocator = VK_NULL_HANDLE;

		void waitIdle() {
			VK_CHECK_RESULT(vkDeviceWaitIdle(device));
		}

		vktCommandPool* getGraphicsCommandPool() { return graphicsCommandPool; }
		vktCommandPool* getTransferCommandPool() { return transferCommandPool; }

		vktQueue* getGraphicsQueue() { return graphicsQueue; }
		vktQueue* getTransferQueue() { return transferQueue; }

	private:
		void createLogicalDevice(void* pNext, VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, bool useSwapChain) {

			// QUEUE FAMILIES

			// dedicated queues support
			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

			float queuePriority = 1.0f;

			VkDeviceQueueCreateInfo queueInfo{};

			// graphics queue
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value();
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueInfo);

			// generally the device is suitable if it has at least the graphics queue, but here we check for dedicated queues

			// transfer queue
			if (vktPhysicalDevice->queueFamilyIndices.transferFamily.has_value() &&
				vktPhysicalDevice->queueFamilyIndices.transferFamily.value() != vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value()) {
				queueInfo = {};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = vktPhysicalDevice->queueFamilyIndices.transferFamily.value();
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
			else {
				vktPhysicalDevice->queueFamilyIndices.transferFamily = vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value();
			}

			// compute queue
			if (vktPhysicalDevice->queueFamilyIndices.computeFamily.has_value() &&
				vktPhysicalDevice->queueFamilyIndices.computeFamily.value() != vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value()
				&& vktPhysicalDevice->queueFamilyIndices.computeFamily.value() != vktPhysicalDevice->queueFamilyIndices.transferFamily.value()
				) {
				queueInfo = {};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = vktPhysicalDevice->queueFamilyIndices.computeFamily.value();
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
			else {
				vktPhysicalDevice->queueFamilyIndices.computeFamily = vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value();
			}

			VkDeviceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			createInfo.pEnabledFeatures = &enabledFeatures;

			// DEVICE EXTENSIONS

			std::vector<const char*> deviceExtensions(enabledExtensions);

			if (useSwapChain)
			{
				// If the device will be used for presenting to a display via a swapchain we need to request the swapchain extension
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			}

			if (deviceExtensions.size() > 0)
			{
				for (const char* enabledExtension : deviceExtensions)
				{
					if (!vktPhysicalDevice->extensionSupported(enabledExtension)) {
						std::cerr << "Enabled device extension \"" << enabledExtension << "\" is not present at device level\n";
					}
				}

				createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
				createInfo.ppEnabledExtensionNames = deviceExtensions.data();
			}

			if (enableVkValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(vkValidationLayers.size());
				createInfo.ppEnabledLayerNames = vkValidationLayers.data();
			}
			else {
				createInfo.enabledLayerCount = 0;
			}

			createInfo.pNext = pNext;

			if (vkCreateDevice(vktPhysicalDevice->physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
				throw std::runtime_error("failed to create logical device!");
			}

			pDeletionQueue->push_function([=]() { vkDestroyDevice(device, nullptr); });

		}
		void createMemoryAllocator() {
			// create memory allocator
			VmaAllocatorCreateInfo allocatorInfo = {};
			allocatorInfo.physicalDevice = vktPhysicalDevice->physicalDevice;
			allocatorInfo.device = device;
			allocatorInfo.instance = vktPhysicalDevice->instance;
			vmaCreateAllocator(&allocatorInfo, &vmaAllocator);

			pDeletionQueue->push_function([=]() { vmaDestroyAllocator(vmaAllocator);	});
		}

		vktCommandPool* graphicsCommandPool;
		vktCommandPool* transferCommandPool;

		vktQueue* graphicsQueue;
		vktQueue* transferQueue;
	};
}