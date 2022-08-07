#include "vktdevices.h"

namespace vkt {
	// PICK PHYSICAL DEVICE

	std::vector<VkPhysicalDevice> enumeratePhysicalDevices(VkInstance& instance) {
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount == 0) {
			throw std::runtime_error("failed to find GPUs with Vulkan support!");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		return devices;
	}

	VkPhysicalDevice pickSuitablePhysicalDevice(VkInstance& instance, bool (*isDeviceSuitable)(VkPhysicalDevice)) {

		std::vector<VkPhysicalDevice> devices = enumeratePhysicalDevices(instance);

		VkPhysicalDevice physicalDevice;

		for (const auto& device : devices) {
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				break;
			}
		}

		if (physicalDevice == VK_NULL_HANDLE) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}

		return physicalDevice;
	}

	// QUEUE FAMILIES

	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
		QueueFamilyIndices indices;
		// Assign index to queue families that could be found

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto& queueFamily : queueFamilies) {
			auto flags = queueFamily.queueFlags;

			if (flags & VK_QUEUE_GRAPHICS_BIT)
				indices.graphicsFamily = i;
			if (flags & VK_QUEUE_COMPUTE_BIT)
				indices.computeFamily = i;
			if (flags & VK_QUEUE_TRANSFER_BIT)
				indices.transferFamily = i;

			i++;
		}

		return indices;
	}

	// CREATE LOGICAL DEVICE

	VkDevice vktPhysicalDevice::createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, bool useSwapChain) {

		// QUEUE FAMILIES

		// dedicated queues support
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

		float queuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueInfo{};

		// graphics queue
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueInfo);

		// generally the device is suitable if it has at least the graphics queue, but here we check for dedicated queues

		// transfer queue
		if (queueFamilyIndices.transferFamily.has_value() &&
			queueFamilyIndices.transferFamily.value() != queueFamilyIndices.graphicsFamily.value()) {
			queueInfo = {};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = queueFamilyIndices.transferFamily.value();
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueInfo);
		}
		else {
			queueFamilyIndices.transferFamily = queueFamilyIndices.graphicsFamily.value();
		}

		// compute queue
		if (queueFamilyIndices.computeFamily.has_value() &&
			queueFamilyIndices.computeFamily.value() != queueFamilyIndices.graphicsFamily.value()
			&& queueFamilyIndices.computeFamily.value() != queueFamilyIndices.transferFamily.value()
			) {
			queueInfo = {};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueInfo);
		}
		else {
			queueFamilyIndices.computeFamily = queueFamilyIndices.graphicsFamily.value();
		}

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pEnabledFeatures = &enabledFeatures;

		// DEVICE EXTENSIONS

		std::vector<const char*> deviceExtensions(enabledExtensions);

		if (useSwapChain)
		{
			// If the device will be used for presenting to a display via a swapchain we need to request the swapchain extension
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}

		if (deviceExtensions.size() > 0)
		{
			for (const char* enabledExtension : deviceExtensions)
			{
				if (!extensionSupported(enabledExtension)) {
					std::cerr << "Enabled device extension \"" << enabledExtension << "\" is not present at device level\n";
				}
			}

			createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
			createInfo.ppEnabledExtensionNames = deviceExtensions.data();
		}

		if (enableVkValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(vkValidationLayers.size());
			createInfo.ppEnabledLayerNames = vkValidationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}

		VkDevice device;

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
			throw std::runtime_error("failed to create logical device!");
		}

		return device;
	}

	// SHADER MODULES

	VkShaderModule vktDevice::createShaderModule(char* spvPath) {
		const std::vector<char> code = readFile(spvPath);

		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			throw std::runtime_error("failed to create shader module!");
		}

		return shaderModule;
	}


	// COMMAND POOL

	VkCommandPool vktDevice::createCommandPool(uint32_t queueFamilyIndex) {

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamilyIndex;

		VkCommandPool commandPool;

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
			throw std::runtime_error("failed to create command pool!");
		}

		pDeletionQueue->push_function([=]() { vkDestroyCommandPool(device, commandPool, nullptr); });

		return commandPool;
	}

	// COMMAND BUFFER
		/**
	Allocates new command buffer, then it begins.
	@param dedicatedCommandPool if a dedicated command pool is not specified, the command buffer is created from the standard graphics pool.
	*/
	VkCommandBuffer vktDevice::createCommandBuffer(VkCommandPool dedicatedCommandPool) {

		VkCommandBuffer commandBuffer;
		VkCommandPool commandPool = dedicatedCommandPool ? dedicatedCommandPool : this->graphicsCommandPool;

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // Optional
		beginInfo.pInheritanceInfo = nullptr; // Optional

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		pDeletionQueue->push_function([=]() {
			vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
			});

		return commandBuffer;
	}

	/**
	Resets the previous command buffer provided.
	Then it begins.
*/
	void vktDevice::restartCommandBuffer(VkCommandBuffer& previousCommandBuffer) {

		vkResetCommandBuffer(previousCommandBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // Optional
		beginInfo.pInheritanceInfo = nullptr; // Optional

		VK_CHECK_RESULT(vkBeginCommandBuffer(previousCommandBuffer, &beginInfo));
	}

	void vktDevice::submitQueue(VkCommandBuffer commandBuffer, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore, VkQueue queue) {

		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		if (waitSemaphore) {
			VkSemaphore waitSemaphores[] = { waitSemaphore };
			VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages;
		}

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		if (signalSemaphore) {
			VkSemaphore signalSemaphores[] = { signalSemaphore };
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = signalSemaphores;
		}

		if (vkQueueSubmit(queue ? queue : graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}
	}

}