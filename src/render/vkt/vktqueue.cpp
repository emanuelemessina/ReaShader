/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "vktqueue.h"

namespace vkt
{
	// class vktQueue

	Queue::Queue(deletion_queue& deletionQueue, VkDevice device, uint32_t queueFamilyIndex)
		: queueFamilyIndex(queueFamilyIndex), vkDevice(device)
	{
		deletionQueue.push_function([=]() { delete (this); });
		vkGetDeviceQueue(device, queueFamilyIndex, 0, &vkQueue);
	}
} // namespace vkt