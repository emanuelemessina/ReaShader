#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

#include <optional>
#include <fstream>
#include <set>
#include <unordered_map>
#include <assert.h>
#include <array>
#include <deque>
#include <functional>
#include <any>

#include "vk_mem_alloc.h"

#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm.hpp"
#include "gtx/transform.hpp"

using namespace std;

#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::cout << "Fatal : \"" << res << "\" in " << __FILE__ << " at line " << __LINE__ << "\n"; \
		throw std::runtime_error("exception!");																		\
	}																									\
}

namespace vkt {

#ifdef NDEBUG
	const bool enableVkValidationLayers = false;
#else
	const bool enableVkValidationLayers = true;
#endif

	// VALIDATION LAYERS

	const std::vector<const char*> vkValidationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};

	struct vktInitProperties {
		bool supportsBlit = false;
	};

	struct vktDeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function) {
			deletors.push_back(function);
		}

		void flush() {
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
				(*it)(); //call the function
			}

			deletors.clear();
		}
	};

	VkInstance createVkInstance(vktDeletionQueue& deletionQueue, const char* applicationName, const char* engineName);

	std::vector<char> readFile(const std::string& filename);

	template <typename T>
	struct searchable_map {
		void add(std::string key, T obj) {
			objs[key] = obj;
		}
		T* get(std::string key) {
			auto it = objs.find(key);
			if (it == objs.end()) {
				return nullptr;
			}
			else {
				return (T*)&(it->second);
			}
		}
	private:
		std::unordered_map<std::string, T> objs;
	};
}