/**
 * @file
 * @brief gpu::Context: the Vulkan instance, the rendering device and what lives with it.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/context.h"

#include "util/logging.h"

namespace ReaShader::gpu
{
	namespace
	{
		template <typename T> T unwrap(vkb::Result<T> result, const char* what)
		{
			if (!result)
				throw std::runtime_error(std::string(what) + " failed: " + result.error().message());
			return result.value();
		}

#ifndef NDEBUG
		// validation layer messages (debug builds) go to the log
		VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
													  VkDebugUtilsMessageTypeFlagsEXT,
													  const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
		{
			if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
				LOG(WARNING, toFile | toConsole, "Vulkan", "Validation", data->pMessage);
			return VK_FALSE;
		}
#endif

		constexpr uint64_t kFrameTimeoutNs = 2'000'000'000; // a frame that takes longer counts as a GPU hang
	} // namespace

	// -------- instance --------

	void Context::createInstance()
	{
		vkb::InstanceBuilder builder;
		builder.set_app_name("ReaShader").require_api_version(1, 3, 0).set_headless(true);
#ifndef NDEBUG
		builder.request_validation_layers(true)
			.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT)
			.set_debug_callback(onDebugMessage);
#endif
		instance = unwrap(builder.build(), "Vulkan instance creation");

		// usable GPUs: Vulkan 1.3 with dynamic rendering and synchronization2
		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		physicalDevices = unwrap(vkb::PhysicalDeviceSelector(instance)
									 .set_minimum_version(1, 3)
									 .set_required_features_13(features13)
									 .select_devices(),
								 "GPU selection");
	}

	void Context::destroyInstance()
	{
		physicalDevices.clear();
		if (instance.instance)
			vkb::destroy_instance(instance);
		instance = {};
	}

	std::vector<std::string> Context::deviceNames() const
	{
		std::vector<std::string> names;
		for (const vkb::PhysicalDevice& physicalDevice : physicalDevices)
			names.push_back(physicalDevice.name);
		return names;
	}

	// -------- device --------

	void Context::createDevice(size_t index)
	{
		vkbDevice = unwrap(vkb::DeviceBuilder(physicalDevices.at(index)).build(), "Vulkan device creation");
		device = vkbDevice.device;
		queue = unwrap(vkbDevice.get_queue(vkb::QueueType::graphics), "Graphics queue");
		uint32_t queueFamily = unwrap(vkbDevice.get_queue_index(vkb::QueueType::graphics), "Graphics queue");

		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorInfo.instance = instance.instance;
		allocatorInfo.physicalDevice = vkbDevice.physical_device.physical_device;
		allocatorInfo.device = device;
		VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamily;
		VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));

		VkCommandBufferAllocateInfo commandBufferInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		commandBufferInfo.commandPool = commandPool;
		commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandBufferInfo.commandBufferCount = 1;
		VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferInfo, &commandBuffer));

		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

		// enough for a handful of passes: each uses one set (up to two image samplers + a uniform buffer)
		VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
											 { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 } };
		VkDescriptorPoolCreateInfo descriptorPoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolInfo.maxSets = 16;
		descriptorPoolInfo.poolSizeCount = 2;
		descriptorPoolInfo.pPoolSizes = poolSizes;
		VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &sampler));
	}

	void Context::destroyDevice()
	{
		if (!device)
			return;

		vkDeviceWaitIdle(device); // may fail on a lost device: destroy anyway

		vkDestroySampler(device, sampler, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyFence(device, fence, nullptr);
		vkDestroyCommandPool(device, commandPool, nullptr);
		vmaDestroyAllocator(allocator);
		vkb::destroy_device(vkbDevice);

		vkbDevice = {};
		device = VK_NULL_HANDLE;
		allocator = nullptr;
		descriptorPool = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
		queue = VK_NULL_HANDLE;
		commandPool = VK_NULL_HANDLE;
		commandBuffer = VK_NULL_HANDLE;
		fence = VK_NULL_HANDLE;
	}

	// -------- commands --------

	VkCommandBuffer Context::beginCommands()
	{
		VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		return commandBuffer;
	}

	void Context::submitAndWait()
	{
		VK_CHECK(vkEndCommandBuffer(commandBuffer));

		VkCommandBufferSubmitInfo commandBufferInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
		commandBufferInfo.commandBuffer = commandBuffer;

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = &commandBufferInfo;

		VK_CHECK(vkResetFences(device, 1, &fence));
		VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, fence));
		VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, kFrameTimeoutNs));
	}
} // namespace ReaShader::gpu
