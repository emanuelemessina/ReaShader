/**
 * @file
 * @brief Small Vulkan helpers shared by the render code: error checking, buffers, images, pipelines, barriers.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <stdexcept>
#include <string>

// Throws std::runtime_error on a failed Vulkan call.
// Exceptions never leave ReaShaderRenderer's public functions.
#define VK_CHECK(call)                                                                                               \
	do                                                                                                               \
	{                                                                                                                \
		VkResult vkCheckResult = (call);                                                                             \
		if (vkCheckResult != VK_SUCCESS)                                                                             \
			throw std::runtime_error(std::string(#call) + " failed: " + string_VkResult(vkCheckResult));             \
	} while (0)

namespace ReaShader::gpu
{
	// A buffer that stays mapped for its whole lifetime (host-visible memory)
	struct Buffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		void* mapped = nullptr;
		VkDeviceSize size = 0;

		// hostAccess: VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT (CPU writes) or
		//             VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT (CPU reads back)
		void create(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
					VmaAllocationCreateFlags hostAccess);
		void destroy(VmaAllocator allocator);

		// make CPU writes visible to the GPU / GPU writes visible to the CPU (no-ops on coherent memory)
		void flush(VmaAllocator allocator) const;
		void invalidate(VmaAllocator allocator) const;
	};

	// A GPU image with a view: 2D, or a 3D cube (a LUT)
	struct Image
	{
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VkImageView view = VK_NULL_HANDLE;
		VkExtent2D extent{}; // width and height (a cube is size x size x size)

		void create(VkDevice device, VmaAllocator allocator, VkExtent2D extent, VkFormat format,
					VkImageUsageFlags usage, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
		void create3D(VkDevice device, VmaAllocator allocator, uint32_t size, VkFormat format, VkImageUsageFlags usage);
		void destroy(VkDevice device, VmaAllocator allocator);

	  private:
		void _create(VkDevice device, VmaAllocator allocator, VkImageType type, VkExtent3D imageExtent, VkFormat format,
					 VkImageUsageFlags usage, VkImageAspectFlags aspect);
	};

	// A graphics pipeline for dynamic rendering into the frame format:
	// triangle list, no culling, no blending, dynamic viewport and scissor
	struct PipelineDesc
	{
		const uint32_t* vertexSpirv = nullptr;
		size_t vertexWords = 0;
		const uint32_t* fragmentSpirv = nullptr;
		size_t fragmentWords = 0;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		const VkPipelineVertexInputStateCreateInfo* vertexInput = nullptr; // none: the shader makes the vertices
		VkFormat depthFormat = VK_FORMAT_UNDEFINED;						   // undefined: no depth test
	};
	VkPipeline createPipeline(VkDevice device, const PipelineDesc& desc);

	// Image layout transition (synchronization2 barrier).
	// Before sampling, use VK_ACCESS_2_SHADER_READ_BIT, not the narrower VK_ACCESS_2_SHADER_SAMPLED_READ_BIT:
	// Intel's Windows driver (UHD 620) doesn't invalidate its texture cache for the latter, and a pass sampled
	// an image as it was before the previous pass rendered it again (in the same command buffer).
	void transition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from, VkImageLayout to,
					VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
					VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

	// REAPER's 'RGBA' frames are B,G,R,A in memory: the same byte order as this format
	constexpr VkFormat kFrameFormat = VK_FORMAT_B8G8R8A8_UNORM;
	constexpr int kBytesPerPixel = 4;
} // namespace ReaShader::gpu
