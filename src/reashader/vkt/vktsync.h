/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "vkt/vktdevices.h"

namespace  vkt {

	template <typename T>
	concept VkSyncObject = std::same_as<T, VkFence> || std::same_as<T, VkSemaphore>;

	namespace sync {

		template <VkSyncObject s>
		inline void destroySyncObject(Logical::Device* vktDevice, const s obj) {
			if (typeid(obj) == typeid(VkFence))
				vkDestroyFence(vktDevice->vk(), (VkFence)obj, nullptr);
			else if (typeid(obj) == typeid(VkSemaphore))
				vkDestroySemaphore(vktDevice->vk(), (VkSemaphore)obj, nullptr);
		}
		template <VkSyncObject s>
		inline void destroySyncObjects(Logical::Device* vktDevice, const s* first, int count) {
			for (int i = 0; i < count; i++)
				destroySyncObject(vktDevice, *s[i]);
		}

		inline VkFence createFence(Logical::Device* vktDevice, bool signaled, bool pushToDeletionQueue = true) {
			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			if (signaled)
				fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(vktDevice->vk(), &fenceInfo, nullptr, &fence));
			if (pushToDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() {
				destroySyncObject(vktDevice, fence);
					});
			return fence;
		}
		inline VkSemaphore createSemaphore(Logical::Device* vktDevice, bool pushToDeletionQueue = true) {
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkSemaphore semaphore;
			VK_CHECK_RESULT(vkCreateSemaphore(vktDevice->vk(), &semaphoreInfo, nullptr, &semaphore));
			if (pushToDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() {
				destroySyncObject(vktDevice, semaphore);
					});
			return semaphore;
		}
	};

}

