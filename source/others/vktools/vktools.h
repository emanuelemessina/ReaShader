#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

#include <optional>
#include <fstream>
#include <set>

#include <assert.h>
#include <array>
#include <deque>
#include <functional>
#include <any>

#include "vk_mem_alloc.h"

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

	VkInstance createVkInstance(vktDeletionQueue& deletionQueue, char* applicationName, char* engineName);

	struct AllocatedBuffer {
		VkBuffer buffer;
		VmaAllocation allocation;
	};

 std::vector<char> readFile(const std::string& filename) {
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

}