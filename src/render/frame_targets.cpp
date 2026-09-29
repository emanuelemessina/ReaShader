/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "render/frame_targets.h"

#include <cstring>

namespace ReaShader::gpu
{
	namespace
	{
		// rows in a buffer <-> image copy are counted in pixels
		VkBufferImageCopy frameCopyRegion(VkExtent2D extent, int rowBytes)
		{
			VkBufferImageCopy region{};
			region.bufferRowLength = (uint32_t)(rowBytes / kBytesPerPixel);
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageExtent = { extent.width, extent.height, 1 };
			return region;
		}

		// the last row may end right after its pixels, without the row padding
		size_t frameBytes(const FrameView& frame)
		{
			return (size_t)frame.rowBytes * (size_t)(frame.height - 1) + (size_t)frame.width * kBytesPerPixel;
		}
	} // namespace

	void FrameTargets::create(Context& context, const FrameView& inputFrame, const FrameView& outputFrame)
	{
		if (inputFrame.rowBytes % kBytesPerPixel || outputFrame.rowBytes % kBytesPerPixel)
			throw std::runtime_error("Frame rows are not a whole number of pixels");

		extent = { (uint32_t)inputFrame.width, (uint32_t)inputFrame.height };
		inputRowBytes = inputFrame.rowBytes;
		outputRowBytes = outputFrame.rowBytes;

		upload.create(context.allocator, (VkDeviceSize)inputRowBytes * extent.height, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		readback.create(context.allocator, (VkDeviceSize)outputRowBytes * extent.height,
						VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

		input.create(context.device, context.allocator, extent, kFrameFormat,
					 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		output.create(context.device, context.allocator, extent, kFrameFormat,
					  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	}

	void FrameTargets::destroy(Context& context)
	{
		output.destroy(context.device, context.allocator);
		input.destroy(context.device, context.allocator);
		readback.destroy(context.allocator);
		upload.destroy(context.allocator);
		extent = {};
	}

	bool FrameTargets::fits(const FrameView& inputFrame, const FrameView& outputFrame) const
	{
		return extent.width == (uint32_t)inputFrame.width && extent.height == (uint32_t)inputFrame.height &&
			   inputRowBytes == inputFrame.rowBytes && outputRowBytes == outputFrame.rowBytes;
	}

	void FrameTargets::writeInput(Context& context, const FrameView& inputFrame)
	{
		std::memcpy(upload.mapped, inputFrame.pixels, frameBytes(inputFrame));
		upload.flush(context.allocator);
	}

	void FrameTargets::readOutput(Context& context, const FrameView& outputFrame)
	{
		readback.invalidate(context.allocator);
		std::memcpy(outputFrame.pixels, readback.mapped, frameBytes(outputFrame));
	}

	void FrameTargets::recordUpload(VkCommandBuffer commandBuffer)
	{
		transition(commandBuffer, input.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
				   VK_ACCESS_2_TRANSFER_WRITE_BIT);

		VkBufferImageCopy region = frameCopyRegion(extent, inputRowBytes);
		vkCmdCopyBufferToImage(commandBuffer, upload.buffer, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
							   &region);

		transition(commandBuffer, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
				   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	}

	void FrameTargets::recordDownload(VkCommandBuffer commandBuffer)
	{
		transition(commandBuffer, output.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

		VkBufferImageCopy region = frameCopyRegion(extent, outputRowBytes);
		vkCmdCopyImageToBuffer(commandBuffer, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
							   &region);

		// make the copy visible to the CPU read after the fence
		VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = readback.buffer;
		barrier.size = VK_WHOLE_SIZE;

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.bufferMemoryBarrierCount = 1;
		dependency.pBufferMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(commandBuffer, &dependency);
	}
} // namespace ReaShader::gpu
