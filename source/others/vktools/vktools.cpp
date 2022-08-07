#include "vktools.h"

// --- single defines ---

#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_MAPBOX_EARCUT
#include "tiny_obj_loader.h"

#define VMA_IMPLEMENTATION // to enable vma typedefs
#include "vk_mem_alloc.h"

// ------

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

}