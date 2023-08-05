#include "vktbuffers.h"

namespace vkt
{
	namespace Buffers
	{
		static size_t pad_uniform_buffer_size(Physical::Device* vktPhysicalDevice, size_t originalSize)
		{
			// Calculate required alignment based on minimum device offset alignment
			size_t minUboAlignment = vktPhysicalDevice->deviceProperties.limits.minUniformBufferOffsetAlignment;
			size_t alignedSize = originalSize;
			if (minUboAlignment > 0)
			{
				alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
			}
			return alignedSize;
		}

		// AllocatedBuffer

		AllocatedBuffer::AllocatedBuffer(vkt::Logical::Device* vktDevice, bool pushToDeletionQueue)
			: vktDevice(vktDevice), pushToDeletionQueue(pushToDeletionQueue)
		{
			if (pushToDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() { destroy(); });
		}

		AllocatedBuffer* AllocatedBuffer::allocate(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
		{
			// allocate vertex buffer
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.pNext = nullptr;

			bufferInfo.size =
				(VkDeviceSize)pad_uniform_buffer_size(vktDevice->physicalDevice, allocSize);
			bufferInfo.usage = usage;

			VmaAllocationCreateInfo vmaallocInfo = {};
			vmaallocInfo.usage = memoryUsage;

			// allocate the buffer
			VK_CHECK_RESULT(vmaCreateBuffer(vktDevice->vmaAllocator, &bufferInfo, &vmaallocInfo, &(this->buffer),
											&(this->allocation), nullptr));

			return this;
		}

		AllocatedBuffer* AllocatedBuffer::putData(void* data, size_t size)
		{
			// copy vertex data
			void* mapped;
			vmaMapMemory(vktDevice->vmaAllocator, allocation, &mapped);

			memcpy(mapped, data, size);

			vmaUnmapMemory(vktDevice->vmaAllocator, allocation);

			return this;
		}

		void AllocatedBuffer::destroy()
		{
			if (buffer)
				vmaDestroyBuffer(vktDevice->vmaAllocator, this->buffer, this->allocation);
			if (allocation)
				VK_CHECK_RESULT(vmaFlushAllocation(vktDevice->vmaAllocator, this->allocation, 0, VK_WHOLE_SIZE));
			delete (this);
		}
	}
}