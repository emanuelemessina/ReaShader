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