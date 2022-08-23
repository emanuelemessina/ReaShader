#pragma once

#include "vktools/vktdevices.h"

namespace vkt {

	template <typename T>
	concept VkSyncObject = std::same_as<T, VkFence> || std::same_as<T, VkSemaphore>;

	namespace syncObjects {

		template <VkSyncObject s>
		inline void destroySyncObject(vktDevice* vktDevice, const s obj) {
			if (typeid(obj) == typeid(VkFence))
				vkDestroyFence(vktDevice->device, (VkFence)obj, nullptr);
			else if (typeid(obj) == typeid(VkSemaphore))
				vkDestroySemaphore(vktDevice->device, (VkSemaphore)obj, nullptr);
		}
		template <VkSyncObject s>
		inline void destroySyncObjects(vktDevice* vktDevice, const s* first, int count) {
			for (int i = 0; i < count; i++)
				destroySyncObject(vktDevice, *s[i]);
		}

		inline VkFence createFence(vktDevice* vktDevice, bool signaled, bool pushToDeletionQueue = true) {
			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			if (signaled)
				fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(vktDevice->device, &fenceInfo, nullptr, &fence));
			if (pushToDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() {
				destroySyncObject(vktDevice, fence);
					});
			return fence;
		}
		inline VkSemaphore createSemaphore(vktDevice* vktDevice, bool pushToDeletionQueue = true) {
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkSemaphore semaphore;
			VK_CHECK_RESULT(vkCreateSemaphore(vktDevice->device, &semaphoreInfo, nullptr, &semaphore));
			if (pushToDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() {
				destroySyncObject(vktDevice, semaphore);
					});
			return semaphore;
		}
	};

}

