#include "vktools.h"

#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_MAPBOX_EARCUT
#include "tiny_obj_loader.h"

#define VMA_IMPLEMENTATION // to enable vma typedefs
#include "vk_mem_alloc.h"

namespace vkt {

	// VALIDATION LAYERS

	const std::vector<const char*> vkValidationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};

#ifdef NDEBUG
	const bool enableVkValidationLayers = false;
#else
	const bool enableVkValidationLayers = true;
#endif

	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : vkValidationLayers) {
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
				return false;
			}
		}

		return true;
	}

	// CREATE INSTANCE

	VkInstance createVkInstance(vktDeletionQueue& deletionQueue, char* applicationName, char* engineName) {

		if (enableVkValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error("validation layers requested, but not available!");
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = applicationName;
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = engineName;
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		// add extensions here (eg. glfw to interface with window system)
		if (enableVkValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(vkValidationLayers.size());
			createInfo.ppEnabledLayerNames = vkValidationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}

		VkInstance instance;

		VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &instance));

		deletionQueue.push_function([=]() { vkDestroyInstance(instance, nullptr); });

		return instance;
	}

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

	// CREATE ATTACHMENT IMAGE

	/**
	Creates image and binds it to device memory
	*/
	void vktAttachment::createImage(int w, int h, VkImageType type, VkFormat format, VkImageTiling imageTiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memoryProperties) {

		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

		createInfo.imageType = type;
		createInfo.format = format;

		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;

		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.tiling = imageTiling; // VK_IMAGE_TILING_LINEAR

		//createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		createInfo.extent.width = w;
		createInfo.extent.height = h;
		createInfo.extent.depth = 1;

		uint32_t uniqueQueueFamilies[1] = { vktDevice->vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value() };

		createInfo.queueFamilyIndexCount = 1;
		createInfo.pQueueFamilyIndices = uniqueQueueFamilies;

		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.usage = usageFlags;

		if (vkCreateImage(vktDevice->device, &createInfo, nullptr, &image) != VK_SUCCESS) {
			throw std::runtime_error("failed to create image!");
		}

		bindToMemory(memoryProperties);
	}
	void vktAttachment::bindToMemory(VkMemoryPropertyFlags memoryProperties) {
		// bind to memory

		VkMemoryAllocateInfo memAlloc{};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(vktDevice->device, image, &memReqs);

		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vktDevice->vktPhysicalDevice->getMemoryType(memReqs.memoryTypeBits, memoryProperties);

		VK_CHECK_RESULT(vkAllocateMemory(vktDevice->device, &memAlloc, nullptr, &memory));
		VK_CHECK_RESULT(vkBindImageMemory(vktDevice->device, image, memory, 0));
	}
	void vktAttachment::createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectMask) {
		VkImageViewCreateInfo colorImageView{};
		colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorImageView.viewType = type;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = aspectMask;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = image;
		VK_CHECK_RESULT(vkCreateImageView(vktDevice->device, &colorImageView, nullptr, &imageView));
	}

	// ALLOCATED IMAGE

	AllocatedImage::~AllocatedImage() {
		if (imageView)
			vkDestroyImageView(vktDevice->device, imageView, nullptr);
		if (image) {
			vmaDestroyImage(vktDevice->vmaAllocator, image, nullptr);
		}
		vmaFreeMemory(vktDevice->vmaAllocator, allocation);
		vmaFlushAllocation(vktDevice->vmaAllocator, allocation, 0, VK_WHOLE_SIZE);
	}
	void AllocatedImage::createImage(VkExtent2D extent,
		VkImageType type, VkFormat format,
		VkImageTiling imageTiling,
		VkImageUsageFlags usageFlags,
		VmaMemoryUsage memoryUsage,
		VkMemoryPropertyFlags memoryProperties,
		VmaAllocationCreateFlags vmaFlags) {

		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

		createInfo.imageType = type;
		createInfo.format = format;

		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;

		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		createInfo.tiling = imageTiling; // VK_IMAGE_TILING_LINEAR

		//createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		createInfo.extent.width = extent.width;
		createInfo.extent.height = extent.height;
		createInfo.extent.depth = 1;

		uint32_t uniqueQueueFamilies[1] = { vktDevice->vktPhysicalDevice->queueFamilyIndices.graphicsFamily.value() };

		createInfo.queueFamilyIndexCount = 1;
		createInfo.pQueueFamilyIndices = uniqueQueueFamilies;

		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.usage = usageFlags;

		VmaAllocationCreateInfo alloccinfo = {};
		alloccinfo.usage = memoryUsage;
		alloccinfo.requiredFlags = VkMemoryPropertyFlags(memoryProperties);
		alloccinfo.flags = vmaFlags;

		VK_CHECK_RESULT(vmaCreateImage(vktDevice->vmaAllocator, &createInfo, &alloccinfo, &image, &allocation, &allocationInfo));
	}
	void AllocatedImage::createImageView(VkImageViewType type, VkFormat format, VkImageAspectFlagBits aspectFlags) {
		VkImageViewCreateInfo colorImageView{};
		colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorImageView.viewType = type;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = aspectFlags;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = image;
		VK_CHECK_RESULT(vkCreateImageView(vktDevice->device, &colorImageView, nullptr, &imageView));
	}

	// SHADER MODULES

	static std::vector<char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			throw std::runtime_error("failed to open file!");
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);

		file.close();

		return buffer;
	}

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

		return commandPool;
	}

	// COMMAND BUFFER
		/**
	Allocates new command buffer, then it begins.
	@param dedicatedCommandPool if a dedicated command pool is not specified, the command buffer is created from the standard graphics pool.
	*/
	VkCommandBuffer vktDevice::createCommandBuffer(VkCommandPool dedicatedCommandPool) {

		VkCommandBuffer commandBuffer;
		VkCommandPool commandPool = dedicatedCommandPool ? dedicatedCommandPool : this->commandPool;

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
	@param dedicatedCommandPool if a dedicated command pool is not specified, the command buffer is created from the standard graphics pool.
	*/
	void vktDevice::restartCommandBuffer(VkCommandBuffer& previousCommandBuffer, VkCommandPool dedicatedCommandPool) {

		VkCommandPool commandPool = dedicatedCommandPool ? dedicatedCommandPool : this->commandPool;

		vkResetCommandBuffer(previousCommandBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // Optional
		beginInfo.pInheritanceInfo = nullptr; // Optional

		VK_CHECK_RESULT(vkBeginCommandBuffer(previousCommandBuffer, &beginInfo));
	}

	void vktDevice::submitQueue(VkCommandBuffer commandBuffer, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore) {

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


		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}
	}

	void insertImageMemoryBarrier(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkImageSubresourceRange subresourceRange)
	{
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.srcAccessMask = srcAccessMask;
		imageMemoryBarrier.dstAccessMask = dstAccessMask;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		vkCmdPipelineBarrier(
			cmdbuffer,
			srcStageMask,
			dstStageMask,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);
	}

	VertexInputDescription Vertex::get_vertex_description()
	{
		VertexInputDescription description;

		//we will have just 1 vertex buffer binding, with a per-vertex rate
		VkVertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = sizeof(Vertex);
		mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		description.bindings.push_back(mainBinding);

		//Position will be stored at Location 0
		VkVertexInputAttributeDescription positionAttribute = {};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = offsetof(Vertex, position);

		//Normal will be stored at Location 1
		VkVertexInputAttributeDescription normalAttribute = {};
		normalAttribute.binding = 0;
		normalAttribute.location = 1;
		normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		normalAttribute.offset = offsetof(Vertex, normal);

		//Color will be stored at Location 2
		VkVertexInputAttributeDescription colorAttribute = {};
		colorAttribute.binding = 0;
		colorAttribute.location = 2;
		colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		colorAttribute.offset = offsetof(Vertex, color);

		description.attributes.push_back(positionAttribute);
		description.attributes.push_back(normalAttribute);
		description.attributes.push_back(colorAttribute);

		return description;
	}

	/**
	All objects will get merged into one Mesh object. The object must be triangulated.
	*/
	bool Mesh::load_from_obj(const char* filename) {

		tinyobj::ObjReaderConfig reader_config;
		tinyobj::ObjReader reader;

		if (!reader.ParseFromFile(filename, reader_config)) {
			if (!reader.Error().empty()) {
				std::cerr << "TinyObjReader: " << reader.Error();
			}
			return false;
		}

		if (!reader.Warning().empty()) {
			std::cout << "TinyObjReader: " << reader.Warning();
		}

		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();
		auto& materials = reader.GetMaterials();

		//------

		// Loop over shapes
		for (size_t s = 0; s < shapes.size(); s++) {
			// Loop over faces(polygon)
			size_t index_offset = 0;
			for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {

				size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

				// Loop over vertices in the face.
				for (size_t v = 0; v < fv; v++) {

					Vertex new_vert; // vertex struct to copy data to

					// access to vertex
					tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

					//vertex position
					tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
					tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
					tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

					new_vert.position.x = vx;
					new_vert.position.y = vy;
					new_vert.position.z = vz;

					// Check if `normal_index` is zero or positive. negative = no normal data
					if (idx.normal_index >= 0) {
						tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
						tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
						tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];

						new_vert.normal.x = nx;
						new_vert.normal.y = ny;
						new_vert.normal.z = nz;
					}

					// Check if `texcoord_index` is zero or positive. negative = no texcoord data
					if (idx.texcoord_index >= 0) {
						tinyobj::real_t tx = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
						tinyobj::real_t ty = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
					}

					//we are setting the vertex color as the vertex normal. This is just for display purposes
					new_vert.color = new_vert.normal;

					vertices.push_back(new_vert);
				}
				index_offset += fv;

				// per-face material
				shapes[s].mesh.material_ids[f];
			}
		}

		return true;
	}

}