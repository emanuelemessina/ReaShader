/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "vktdevices.h"

namespace vkt
{

	namespace Buffers
	{

		static size_t pad_uniform_buffer_size(Physical::Device* vktPhysicalDevice, size_t originalSize);

		class AllocatedBuffer
		{
		  private:
			Logical::Device* vktDevice;
			bool pushToDeletionQueue;

			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = nullptr;

		  public:
			AllocatedBuffer(Logical::Device* vktDevice, bool pushToDeletionQueue = true);

			/**
			Does automatic padding alignment
			*/
			AllocatedBuffer* allocate(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

			AllocatedBuffer* map(void** data)
			{
				VK_CHECK_RESULT(vmaMapMemory(vktDevice->vmaAllocator, allocation, data));
			}

			AllocatedBuffer* unmap()
			{
				vmaUnmapMemory(vktDevice->vmaAllocator, allocation);		
				return this;
			}

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
	}; // namespace Buffers

} // namespace vkt