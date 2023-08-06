#pragma once

#include "vktcommon.h"

namespace vkt
{
	class Queue : IVkWrapper<VkQueue>
	{
	  public:
		Queue(deletion_queue& deletionQueue, VkDevice device, uint32_t queueFamilyIndex);

		VkQueue vk()
		{
			return vkQueue;
		}
		uint32_t getFamilyIndex()
		{
			return queueFamilyIndex.value();
		}

		void waitIdle()
		{
			VK_CHECK_RESULT(vkQueueWaitIdle(vkQueue));
		}

		VkDevice getVkDevice()
		{
			return vkDevice;
		}

	  private:
		VkDevice vkDevice = VK_NULL_HANDLE;
		VkQueue vkQueue = VK_NULL_HANDLE;
		std::optional<uint32_t> queueFamilyIndex;
	};
} // namespace vkt
