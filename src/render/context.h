/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "render/gpu.h"

#include <VkBootstrap.h>

#include <string>
#include <vector>

namespace ReaShader::gpu
{
	// The Vulkan instance and the device frames are rendered on:
	// - instance: created once, lists the usable GPUs
	// - device: one queue, one command buffer + fence (frames are rendered one at a time),
	//   the memory allocator, a descriptor pool and a sampler shared by the passes
	struct Context
	{
		void createInstance();
		void destroyInstance();

		// names of the usable GPUs, in the order createDevice() indexes them
		std::vector<std::string> deviceNames() const;

		void createDevice(size_t index);
		void destroyDevice();
		bool hasDevice() const
		{
			return device != VK_NULL_HANDLE;
		}

		// record into the returned command buffer, then submitAndWait()
		VkCommandBuffer beginCommands();
		void submitAndWait();

		VkDevice device = VK_NULL_HANDLE;
		VmaAllocator allocator = nullptr;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;

	  private:
		vkb::Instance instance;
		std::vector<vkb::PhysicalDevice> physicalDevices;
		vkb::Device vkbDevice;

		VkQueue queue = VK_NULL_HANDLE;
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
	};
} // namespace ReaShader::gpu
