#include "vktdevices.h"
#include "vktcommandpool.h"
#include "vktqueue.h"

namespace vkt
{
	namespace Physical
	{
		QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physicalDevice)
		{

			// get families

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

			Physical::QueueFamilyIndices queueFamilyIndices;

			// get num of assignable queues (how many bits are set)

			std::vector<int> queueFamiliesBitMatches(queueFamilyCount);

			for (uint32_t i = 0; i < queueFamilyCount; i++)
			{

				auto flags = queueFamilies[i].queueFlags;

				int* m = &queueFamiliesBitMatches[i];

				if (flags & VK_QUEUE_GRAPHICS_BIT)
					(*m)++;
				if (flags & VK_QUEUE_COMPUTE_BIT)
					(*m)++;
				if (flags & VK_QUEUE_TRANSFER_BIT)
					(*m)++;
			}

			// sort found queuefamilies based on num matches (ascending)

			std::vector<int> sortedIndices(queueFamilyCount);
			std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
			std::sort(sortedIndices.begin(), sortedIndices.end(),
					  [&](int A, int B) -> bool { return queueFamiliesBitMatches[A] < queueFamiliesBitMatches[B]; });

			// loop thru sorted queues
			// fewest matches first -> means that family is more specialized than one with more matches (generic family)
			// once queue is assigned it's not assignable anymore and it's taken by that family
			// sorting ensures that specialized families come first

			bool graphicsAssignable = true, computeAssignable = true, transferAssignable = true;

			for (uint32_t i = 0; i < queueFamilyCount; i++)
			{

				auto flags = queueFamilies[sortedIndices[i]].queueFlags;

				if (flags & VK_QUEUE_GRAPHICS_BIT && graphicsAssignable)
				{
					queueFamilyIndices.graphicsFamily = sortedIndices[i];
					graphicsAssignable = false;
				}
				if (flags & VK_QUEUE_COMPUTE_BIT && computeAssignable)
				{
					queueFamilyIndices.computeFamily = sortedIndices[i];
					computeAssignable = false;
				}
				if (flags & VK_QUEUE_TRANSFER_BIT && transferAssignable)
				{
					queueFamilyIndices.transferFamily = sortedIndices[i];
					transferAssignable = false;
				}
			}

			return queueFamilyIndices;
		}

		// deviceSelector

		DeviceSelector& DeviceSelector::enumerate(VkInstance instance)
		{
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

			if (deviceCount == 0)
			{
				throw std::runtime_error("failed to find GPUs with Vulkan support!");
			}

			m_physicalDevices.resize(deviceCount);

			vkEnumeratePhysicalDevices(instance, &deviceCount, m_physicalDevices.data());

			return *this;
		}

		DeviceSelector& DeviceSelector::removeUnsuitable(bool (*isDeviceSuitable)(VkPhysicalDevice))
		{
			m_physicalDevices.erase(std::remove_if(m_physicalDevices.begin(), m_physicalDevices.end(),
												   [&](const VkPhysicalDevice pD) { return !isDeviceSuitable(pD); }),
									m_physicalDevices.end());

			if (m_physicalDevices.empty())
			{
				throw std::runtime_error("failed to find a suitable GPU!");
			}

			return *this;
		}

		// physicalDevice

		Device::Device(deletion_queue& deletionQueue, VkInstance instance, VkPhysicalDevice vkPhysicalDevice)
			: vkPhysicalDevice(vkPhysicalDevice), instance(instance)
		{

			queueFamilyIndices = Physical::findQueueFamilies(vkPhysicalDevice);

			vkGetPhysicalDeviceProperties(vkPhysicalDevice, &deviceProperties);
			vkGetPhysicalDeviceFeatures(vkPhysicalDevice, &deviceFeatures);

			// Memory properties are used regularly for creating all kinds of buffers
			vkGetPhysicalDeviceMemoryProperties(vkPhysicalDevice, &memoryProperties);

			// Get list of supported extensions
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &extCount, nullptr);
			if (extCount > 0)
			{
				std::vector<VkExtensionProperties> extensions(extCount);
				if (vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &extCount, &extensions.front()) ==
					VK_SUCCESS)
				{
					for (const auto& ext : extensions)
					{
						supportedExtensions.push_back(ext.extensionName);
					}
				}
			}

			deletionQueue.push_function([=]() { delete (this); });
		}

		uint32_t Device::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound)
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

		bool Device::extensionSupported(std::string extension)
		{
			return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) !=
					supportedExtensions.end());
		}

		VkFormatProperties Device::getFormatProperties(VkFormat format)
		{
			VkFormatProperties formatProps;
			vkGetPhysicalDeviceFormatProperties(vkPhysicalDevice, format, &formatProps);
			return formatProps;
		}

		bool Device::supportsBlit()
		{

			static std::optional<bool> supportsBlit;

			if (supportsBlit.has_value())
				return supportsBlit.value();

			// Check blit support for source and destination
			supportsBlit =
				(getFormatProperties(VK_FORMAT_R8G8B8A8_UNORM).linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

			return supportsBlit.value();
		}
	} // namespace Physical

	namespace Logical
	{
		Device::Device(deletion_queue& deletionQueue, Physical::Device* physicalDevice,
					   VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions,
					   bool useSwapChain)
			: physicalDevice(physicalDevice), pDeletionQueue(&deletionQueue)
		{

			deletionQueue.push_function([=]() { delete (this); });

			VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
			shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
			shader_draw_parameters_features.pNext = nullptr;
			shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;

			createLogicalDevice(&shader_draw_parameters_features, enabledFeatures, enabledExtensions, useSwapChain);

			graphicsQueue =
				new Queue(deletionQueue, vkDevice, physicalDevice->queueFamilyIndices.graphicsFamily.value());
			transferQueue =
				new Queue(deletionQueue, vkDevice, physicalDevice->queueFamilyIndices.transferFamily.value());

			graphicsCommandPool = new CommandPool(deletionQueue, graphicsQueue);
			transferCommandPool = new CommandPool(deletionQueue, transferQueue);

			createMemoryAllocator();
		}

		void Device::createLogicalDevice(void* pNext, VkPhysicalDeviceFeatures enabledFeatures,
										 std::vector<const char*> enabledExtensions, bool useSwapChain)
		{

			// QUEUE FAMILIES

			// dedicated queues support
			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

			float queuePriority = 1.0f;

			VkDeviceQueueCreateInfo queueInfo{};

			// graphics queue
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = physicalDevice->queueFamilyIndices.graphicsFamily.value();
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueInfo);

			// generally the device is suitable if it has at least the graphics queue, but here we check for
			// dedicated queues

			// transfer queue
			if (physicalDevice->queueFamilyIndices.transferFamily.has_value() &&
				physicalDevice->queueFamilyIndices.transferFamily.value() !=
					physicalDevice->queueFamilyIndices.graphicsFamily.value())
			{
				queueInfo = {};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = physicalDevice->queueFamilyIndices.transferFamily.value();
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
			else
			{
				physicalDevice->queueFamilyIndices.transferFamily =
					physicalDevice->queueFamilyIndices.graphicsFamily.value();
			}

			// compute queue
			if (physicalDevice->queueFamilyIndices.computeFamily.has_value() &&
				physicalDevice->queueFamilyIndices.computeFamily.value() !=
					physicalDevice->queueFamilyIndices.graphicsFamily.value() &&
				physicalDevice->queueFamilyIndices.computeFamily.value() !=
					physicalDevice->queueFamilyIndices.transferFamily.value())
			{
				queueInfo = {};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = physicalDevice->queueFamilyIndices.computeFamily.value();
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
			else
			{
				physicalDevice->queueFamilyIndices.computeFamily =
					physicalDevice->queueFamilyIndices.graphicsFamily.value();
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
				// If the device will be used for presenting to a display via a swapchain we need to request the
				// swapchain extension
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			}

			if (deviceExtensions.size() > 0)
			{
				for (const char* enabledExtension : deviceExtensions)
				{
					if (!physicalDevice->extensionSupported(enabledExtension))
					{
						std::cerr << "Enabled device extension \"" << enabledExtension
								  << "\" is not present at device level\n";
					}
				}

				createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
				createInfo.ppEnabledExtensionNames = deviceExtensions.data();
			}

			if (enableVkValidationLayers)
			{
				createInfo.enabledLayerCount = static_cast<uint32_t>(vkValidationLayers.size());
				createInfo.ppEnabledLayerNames = vkValidationLayers.data();
			}
			else
			{
				createInfo.enabledLayerCount = 0;
			}

			createInfo.pNext = pNext;

			if (vkCreateDevice(physicalDevice->vk(), &createInfo, nullptr, &vkDevice) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create logical device!");
			}

			pDeletionQueue->push_function([=]() { vkDestroyDevice(vkDevice, nullptr); });
		}

		void Device::createMemoryAllocator()
		{
			// create memory allocator
			VmaAllocatorCreateInfo allocatorInfo = {};
			allocatorInfo.physicalDevice = physicalDevice->vk();
			allocatorInfo.device = vkDevice;
			allocatorInfo.instance = physicalDevice->instance;
			vmaCreateAllocator(&allocatorInfo, &vmaAllocator);

			pDeletionQueue->push_function([=]() { vmaDestroyAllocator(vmaAllocator); });
		}

		CommandPool* Device::getGraphicsCommandPool()
		{
			return graphicsCommandPool;
		}
		CommandPool* Device::getTransferCommandPool()
		{
			return transferCommandPool;
		}

		Queue* Device::getGraphicsQueue()
		{
			return graphicsQueue;
		}
		Queue* Device::getTransferQueue()
		{
			return transferQueue;
		}
	} // namespace Logical
} // namespace vkt