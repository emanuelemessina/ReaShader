#pragma once

#include "vktdevices.h"

namespace vkt {

	namespace Buffers {

		static size_t pad_uniform_buffer_size(Physical::Device* vktPhysicalDevice, size_t originalSize);

		class AllocatedBuffer
		{
		  private:
			Logical::Device* vktDevice;
			bool pushToDeletionQueue;

			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;

		  public:
			AllocatedBuffer(vkt::Logical::Device* vktDevice, bool pushToDeletionQueue = true);

			/**
			Does automatic padding alignment
			*/
			AllocatedBuffer* allocate(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

			AllocatedBuffer* putData(void* data, size_t size);

			void destroy();

			VkBuffer getBuffer()
			{
				return buffer;
			}
			VmaAllocation getAllocation()
			{
				return allocation;
			}
		};
	};

	
}