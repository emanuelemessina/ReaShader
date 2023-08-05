#pragma once

#include "vktools/vktcommon.h"

namespace vkt {

	FWD_DECL(CommandPool);
	FWD_DECL(Queue);

	namespace Physical
	{
		struct QueueFamilyIndices
		{
			std::optional<uint32_t> graphicsFamily;
			std::optional<uint32_t> transferFamily;
			std::optional<uint32_t> computeFamily;
		};
	
		QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physicalDevice);
		
		class DeviceSelector
		{
		  public:
			DeviceSelector& enumerate(VkInstance instance);

			DeviceSelector& removeUnsuitable(bool (*isDeviceSuitable)(VkPhysicalDevice));
			
			std::vector<VkPhysicalDevice> get()
			{
				return m_physicalDevices;
			}

		  private:
			std::vector<VkPhysicalDevice> m_physicalDevices{};
		};

		/**
Instantiate a helper object with all the info about the physicalDevice specified, so you don't have to query for
properties each time, they're already here upon construction.
*/
		class Device : IVkWrapper<VkPhysicalDevice>
		{
		  private:
			VkPhysicalDevice vkPhysicalDevice;

		  public:
			Device(deletion_queue& deletionQueue, VkInstance instance, VkPhysicalDevice physicalDevice);
			~Device() = default;

			VkInstance instance;

			Physical::QueueFamilyIndices queueFamilyIndices;

			VkPhysicalDeviceProperties deviceProperties;
			VkPhysicalDeviceFeatures deviceFeatures;

			/** @brief Memory types and heaps of the physical device */
			VkPhysicalDeviceMemoryProperties memoryProperties;

			/** @brief List of extensions supported by the device */
			std::vector<std::string> supportedExtensions;

			// -----

			VkPhysicalDevice vk()
			{
				return vkPhysicalDevice;
			}

			/**
			 * Get the index of a memory type that has all the requested property bits set
			 *
			 * @param typeBits Bit mask with bits set for each memory type supported by the resource to request for
			 * (from VkMemoryRequirements)
			 * @param properties Bit mask of properties for the memory type to request
			 * @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been
			 * found
			 *
			 * @return Index of the requested memory type
			 *
			 * @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the
			 * requested properties
			 */
			uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties,
								   VkBool32* memTypeFound = nullptr);

			/**
			 * Check if an extension is supported by the (physical device)
			 *
			 * @param extension Name of the extension to check
			 *
			 * @return True if the extension is supported (present in the list read at device creation time)
			 */
			bool extensionSupported(std::string extension);

			VkFormatProperties getFormatProperties(VkFormat format);

			bool supportsBlit();
		};

	}
		
	namespace Logical
	{
		/**
 Instantiate a logical VkDevice from the device and options provided.
 Pull a graphics queue and create a graphics command pool upon construction.
 */
		class Device : IVkWrapper<VkDevice>
		{
		  private:
			VkDevice vkDevice = VK_NULL_HANDLE;

		  public:
			Device(deletion_queue& deletionQueue, Physical::Device* physicalDevice,
				   VkPhysicalDeviceFeatures enabledFeatures = {}, std::vector<const char*> enabledExtensions = {},
				   bool useSwapChain = false);

			Physical::Device* physicalDevice;

			deletion_queue* pDeletionQueue;

			VmaAllocator vmaAllocator = VK_NULL_HANDLE;

			VkDevice vk()
			{
				return vkDevice;
			}

			void waitIdle()
			{
				VK_CHECK_RESULT(vkDeviceWaitIdle(vkDevice));
			}

			CommandPool* getGraphicsCommandPool();
			CommandPool* getTransferCommandPool();
			Queue* getGraphicsQueue();
			Queue* getTransferQueue();

		  private:
			void createLogicalDevice(void* pNext, VkPhysicalDeviceFeatures enabledFeatures,
									 std::vector<const char*> enabledExtensions, bool useSwapChain);

			void createMemoryAllocator();

			CommandPool* graphicsCommandPool;
			CommandPool* transferCommandPool;

			Queue* graphicsQueue;
			Queue* transferQueue;
		};
	}
}