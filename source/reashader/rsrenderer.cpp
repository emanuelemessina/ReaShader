/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "rsrenderer.h"
#include "rsparams.h"
#include "rsprocessor.h"
#include "tools/compiler_codes.h"
#include "tools/exceptions.h"

/* lice */
#pragma warning(push)
#pragma warning(disable : W_RESULT_OF_ARITHMETIC_OPERATION_CAST_TO_LARGER_SIZE)
#pragma warning(disable : W_FUNC_MAY_BE_UNSAFE)

#include "lice/lice.h"

#pragma warning(pop)
/* ---- */

#define MAX_OBJECTS 100

#include "tools/paths.h"
#include <tools/exceptions.h>
#include <tools/logging.h>

#define GET_ASSET_DIR(asset_dirname) tools::paths::join({ ASSETS_DIR, asset_dirname })

#define MESHES_DIR GET_ASSET_DIR("meshes")
#define SHADERS_DIR GET_ASSET_DIR("shaders")
#define IMAGES_DIR GET_ASSET_DIR("images")

#include "vkt/vktcommandpool.h"
#include "vkt/vktcommands.h"
#include "vkt/vktpipeline.h"
#include "vkt/vktqueue.h"
#include "vkt/vkttextures.h"

namespace ReaShader
{
	ReaShaderRenderer::ReaShaderRenderer(ReaShaderProcessor* reaShaderProcessor)
		: reaShaderProcessor(reaShaderProcessor)
	{
	}

	void ReaShaderRenderer::_initVulkanGuarded()
	{
		WRAP_LOW_LEVEL_FAULTS(_initVulkan();, "ReaShaderRenderer", "Fatal Error", "Caught during initVulkan")
	}

	void ReaShaderRenderer::init()
	{
		// init vulkan

		try
		{
			_initVulkanGuarded();
		}
		catch (STDEXC e)
		{
			exceptionOnInitialize = true;
			LOG(e, toFile | toConsole | toBox, "ReaShaderRenderer", "Exception: ", "ReaShader crashed...");
		}
	}

	void ReaShaderRenderer::_cleanupVulkanGuarded()
	{
		WRAP_LOW_LEVEL_FAULTS(_cleanupVulkan();, "ReaShaderError", "Fatal Error", "Caught in cleanupVulkan")
	}

	void ReaShaderRenderer::shutdown()
	{
		// clean up vulkan

		if (exceptionOnInitialize)
			return;

		try
		{
			_cleanupVulkan();
		}
		catch (STDEXC e)
		{
			LOG(e, toFile | toConsole | toBox, "ReaShaderRenderer", "Exception: ", "ReaShader crashed...");
		}
	}

	// reutilized voids

	void ReaShaderRenderer::checkFrameSize(int& w, int& h, void (*listener)())
	{
		if (halted)
			return;

		// assume if width changed all aspec ratio changed, come on...
		if (w != FRAME_W)
		{
			// update frame size
			FRAME_W = w;
			FRAME_H = h;

			// wait, flush, recreate
			vktDevice->getGraphicsQueue()->waitIdle();
			vktFrameResizedDeletionQueue.flush();
			_createRenderTargets();

			// call listener
			if (listener)
				listener();
		}
	}

	struct ids
	{
		enum descriptorBindings
		{
			gUb = 0,
			gUbD,
			oSb = 0,
			tCis = 0,
			binding_sampled_frame
		};

		enum commandBuffers
		{
			draw,
			transfer,
		};

		enum meshes
		{
			triangle,
			suzanne,
			quad,
			reashader
		};

		enum textures
		{
			logo
		};

		enum materials
		{
			opaque,
			post_process
		};
	} ids;

	// DRAW

	struct MeshPushConstants
	{
		glm::int32 objectId;
		glm::float32 videoParam;
	};

	struct GPUCameraData
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 viewproj;
	};

	struct GPUSceneData
	{
		glm::vec4 fogColor;		// w is for exponent
		glm::vec4 fogDistances; // x for min, y for max, zw unused.
		glm::vec4 ambientColor;
		glm::vec4 sunlightDirection; // w for sun power
		glm::vec4 sunlightColor;
	};

	struct GPUObjectData
	{
		glm::mat4 finalModelMatrix;
	};

	void ReaShaderRenderer::drawFrame(double pushConstants[])
	{
		if (halted)
			return;

		vkt::CommandPool* commandPool = vktDevice->getGraphicsCommandPool();
		VkCommandBuffer commandBuffer = vkDrawCommandBuffer;
		VkExtent2D extent{ FRAME_W, FRAME_H };

		// begin command buffer
		commandPool->restartCommandBuffer(commandBuffer);

		// begin render pass
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = vkRenderPass;
		renderPassInfo.framebuffer = vkFramebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		VkClearValue clearColor = { { { 0.0f, 0.0f, 0.0f, 0.0f } } }; // transparent

		// clear depth at 1
		VkClearValue depthClear{};
		depthClear.depthStencil.depth = 1.f;

		std::vector<VkClearValue> clearValues = { clearColor, depthClear };

		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// render objects

		double proj_time = pushConstants[0];
		double frameNumber = proj_time * pushConstants[1]; // proj_time * frate

		// camera position
		glm::vec3 camPos = { 0.f, 0.f, -5.f };
		glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
		// camera projection
		glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.0f);
		// projection[1][1] *= -1;

		// fill a GPU camera data struct
		GPUCameraData camData{};
		camData.proj = projection;
		camData.view = view;
		camData.viewproj = projection * view;

		GPUSceneData sceneData{};

		// and copy it to the buffer
		int* data{ nullptr };
		VK_CHECK_RESULT(vmaMapMemory(vktDevice->vmaAllocator, frameData.cameraBuffer->getAllocation(), (void**)&data));

		memcpy(data, &camData, sizeof(GPUCameraData));

		vmaUnmapMemory(vktDevice->vmaAllocator, frameData.cameraBuffer->getAllocation());

		VK_CHECK_RESULT(vmaMapMemory(vktDevice->vmaAllocator, frameData.sceneBuffer->getAllocation(), (void**)&data));

		data += 0; // dynamic buffer offset

		memcpy(data, &sceneData, sizeof(GPUSceneData));

		vmaUnmapMemory(vktDevice->vmaAllocator, frameData.sceneBuffer->getAllocation());

		std::vector<uint32_t> dynamicOffsets = {
			0
		}; // offset for each binding to a dynamic descriptor, in order of binding registration

		glm::mat4 modelTransform = glm::rotate(glm::mat4{ 1.0f }, (float)glm::radians(frameNumber * 1.f),
											   glm::vec3(0.1f * sin(proj_time), 1, 0.05f * cos(proj_time)));

		VK_CHECK_RESULT(vmaMapMemory(vktDevice->vmaAllocator, frameData.objectBuffer->getAllocation(), (void**)&data));

		GPUObjectData* objectSSBO = (GPUObjectData*)data;

		vkt::Rendering::Mesh* lastMesh = nullptr;
		vkt::Rendering::Material* lastMaterial = nullptr;
		for (int i = 0; i < renderObjects.size(); i++)
		{
			vkt::Rendering::RenderObject& object = renderObjects[i];

			// write storage buffers
			objectSSBO[i].finalModelMatrix = modelTransform * object.localTransformMatrix;

			// only bind the pipeline if it doesn't match with the already bound one
			if (object.material != lastMaterial)
			{
				// dynamic states

				VkViewport viewport{};
				viewport.x = 0.0f;
				viewport.y = static_cast<float>(extent.height);
				viewport.width = static_cast<float>(extent.width);
				viewport.height = -static_cast<float>(extent.height); // flipping viewport for vulkan :* <3 UwU
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor{};
				scissor.offset = { 0, 0 };
				scissor.extent = extent;
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
				lastMaterial = object.material;

				// bind the descriptor set when changing pipeline
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout,
										0, 1, &frameData.globalSet.set, static_cast<uint32_t>(dynamicOffsets.size()),
										dynamicOffsets.data());

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout,
										1, 1, &frameData.objectSet.set, 0, nullptr);

				if (object.material->textureSet != VK_NULL_HANDLE)
				{
					VkDescriptorSet textureSet = object.material->textureSet->set;
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
											object.material->pipelineLayout, 2, 1, &textureSet, 0, nullptr);
				}
			}

			// set push constants

			// model rotation
			MeshPushConstants constants{};
			constants.objectId = i;
			constants.videoParam = pushConstants[2];

			// upload the mesh to the GPU via push constants
#pragma warning(suppress : W_PTR_MIGHT_BE_NULL) // assert material is not nullptr
			vkCmdPushConstants(commandBuffer, object.material->pipelineLayout,
							   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants),
							   &constants);

			// only bind the mesh if it's a different one from last bind

			if (object.mesh != lastMesh)
			{
				// bind the mesh vertex buffer with offset 0
				VkDeviceSize offset = 0;
				VkBuffer vertexBuffer = object.mesh->getVertexBuffer()->getBuffer();
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vertexBuffer), &offset);

				// only bind index buffer if it's used
				if (object.mesh->getIndexBuffer()->getBuffer())
					vkCmdBindIndexBuffer(commandBuffer, object.mesh->getIndexBuffer()->getBuffer(), 0,
										 VK_INDEX_TYPE_UINT32);

				lastMesh = object.mesh;
			}

			// we can now draw

			if (object.mesh->getIndexBuffer()->getBuffer())
				vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(object.mesh->getIndices().size()), 1, 0, 0, 0);
			else
				vkCmdDraw(commandBuffer, static_cast<uint32_t>(object.mesh->getVertices().size()), 1, 0, 0);
		}

		// unmap storage buffers
		vmaUnmapMemory(vktDevice->vmaAllocator, frameData.objectBuffer->getAllocation());

		// end render pass

		vkCmdEndRenderPass(commandBuffer);

		// submit ( end command buffer )

		commandPool->submit(commandBuffer, VK_NULL_HANDLE, vkRenderFinishedSemaphore, vkImageAvailableSemaphore);
	}

	void ReaShaderRenderer::transferFrame(int*& destBuffer)
	{
		if (halted)
			return;

		// init command buffer

		VkCommandBuffer commandBuffer = vkTransferCommandBuffer;
		vkt::CommandPool* commandPool = vktDevice->getGraphicsCommandPool();

		VK_CHECK_RESULT(vkQueueWaitIdle(vktDevice->getGraphicsQueue()->vk()));

		commandPool->restartCommandBuffer(commandBuffer);

		// Transition destination image to transfer destination layout

		vkt::commands::insertImageMemoryBarrier(
			commandBuffer, vktFrameTransfer->getImage(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// srcImage is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need to be transitioned

		// do the blit/copy

		// If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from
		// BGR to RGB)
		if (vktDevice->physicalDevice->supportsBlit())
		{
			// Define the region to blit (we will blit the whole swapchain image)
			VkOffset3D blitSize{};
			blitSize.x = FRAME_W;
			blitSize.y = FRAME_H;
			blitSize.z = 1;
			VkImageBlit imageBlitRegion{};
			imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.srcSubresource.layerCount = 1;
			imageBlitRegion.srcOffsets[1] = blitSize;
			imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.dstSubresource.layerCount = 1;
			imageBlitRegion.dstOffsets[1] = blitSize;

			// Issue the blit command
			vkCmdBlitImage(commandBuffer, vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlitRegion,
						   VK_FILTER_NEAREST);
		}
		else
		{
			// Otherwise use image copy (requires us to manually flip components)
			VkImageCopy imageCopyRegion{};
			imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageCopyRegion.srcSubresource.layerCount = 1;
			imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageCopyRegion.dstSubresource.layerCount = 1;
			imageCopyRegion.extent = { FRAME_W, FRAME_H };
			imageCopyRegion.extent.depth = 1;

			// Issue the copy command
			vkCmdCopyImage(commandBuffer, vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory
		// later on
		vkt::commands::insertImageMemoryBarrier(
			commandBuffer, vktFrameTransfer->getImage(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit queue (wait for draw frame)

		commandPool->submit(commandBuffer, vkInFlightFence, VK_NULL_HANDLE, vkRenderFinishedSemaphore);

		// wait fence since now we are on cpu

		VK_CHECK_RESULT(vkWaitForFences(vktDevice->vk(), 1, &vkInFlightFence, VK_TRUE, UINT64_MAX))
		VK_CHECK_RESULT(vkResetFences(vktDevice->vk(), 1, &vkInFlightFence))

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;

		vkGetImageSubresourceLayout(vktDevice->vk(), vktFrameTransfer->getImage(), &subResource, &subResourceLayout);

		// dest image is already mapped
		memcpy((void*)destBuffer,
			   (void*)(reinterpret_cast<uintptr_t>((vktFrameTransfer->getAllocationInfo()).pMappedData) +
					   subResourceLayout.offset),
			   sizeof(LICE_pixel) * FRAME_W * FRAME_H);
	}

	// load vf bits to color attachment
	void ReaShaderRenderer::loadBitsToImage(int* srcBuffer)
	{
		if (halted)
			return;

		vkt::CommandPool* commandPool = vktDevice->getGraphicsCommandPool();
		vkt::Queue* queue = vktDevice->getGraphicsQueue();
		VkCommandBuffer commandBuffer = vkTransferCommandBuffer;

		VK_CHECK_RESULT(vkQueueWaitIdle(queue->vk()))

		commandPool->restartCommandBuffer(vkTransferCommandBuffer);

		vkt::commands::transferRawBufferToImage(vktDevice, commandBuffer, srcBuffer, vktFrameTransfer,
												sizeof(LICE_pixel) * FRAME_W * FRAME_H);

		// retransition frametransfer to src copy optimal

		vkt::commands::insertImageMemoryBarrier(vkTransferCommandBuffer, vktFrameTransfer->getImage(),
												VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
												VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
												VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
												VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// color attachment goes dst optimal

		vkt::commands::insertImageMemoryBarrier(
			vkTransferCommandBuffer, vktColorAttachment->getImage(), 0, VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// as well as post process source

		vkt::commands::insertImageMemoryBarrier(
			vkTransferCommandBuffer, vktPostProcessSource->getImage(), 0, VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// perform copy to color attachment

		VkImageCopy imageCopyRegion{};
		imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.srcSubresource.layerCount = 1;
		imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.dstSubresource.layerCount = 1;
		imageCopyRegion.extent.width = FRAME_W;
		imageCopyRegion.extent.height = FRAME_H;
		imageCopyRegion.extent.depth = 1;

		vkCmdCopyImage(vkTransferCommandBuffer, vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);

		// and to post process source

		vkCmdCopyImage(vkTransferCommandBuffer, vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   vktPostProcessSource->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);

		// retransition post process source to shader read optimal

		vkt::commands::insertImageMemoryBarrier(vkTransferCommandBuffer, vktPostProcessSource->getImage(), 0,
												VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
												VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
												VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
												VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		commandPool->submit(vkTransferCommandBuffer, vkInFlightFence, vkImageAvailableSemaphore, VK_NULL_HANDLE);

		// write descriptor for post process source

		VK_CHECK_RESULT(vkWaitForFences(vktDevice->vk(), 1, &vkInFlightFence, VK_TRUE, UINT64_MAX))
		VK_CHECK_RESULT(vkResetFences(vktDevice->vk(), 1, &vkInFlightFence))

		vkt::Descriptors::DescriptorSetWriter(vktDevice)
			.selectDescriptorSet(frameData.textureSet)
			.selectBinding(ids::descriptorBindings::binding_sampled_frame)
			.registerWriteImage(vktPostProcessSource, vkSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.writeRegistered();
	}

	/* vulkan */

	bool isPhysicalDeviceSuitable(VkPhysicalDevice device)
	{
		// physical device properties

		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		// queue families

		vkt::Physical::QueueFamilyIndices indices = vkt::Physical::findQueueFamilies(device);

		// final condition

		return
#ifdef DEBUG
			//deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
#endif
			indices.graphicsFamily.has_value();
	}

	// RENDER PASS

	VkRenderPass createRenderPass(vkt::Logical::Device* vktDevice)
	{
		vkt::Pipeline::RenderPassBuilder renderPassBuilder(vktDevice);

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = VK_FORMAT_B8G8R8A8_UNORM;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // load existing attachment information
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		colorAttachment.finalLayout =
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // transition from dst to src optimal layout after render finished

		VkAttachmentDescription depth_attachment = {};
		depth_attachment.flags = 0;
		depth_attachment.format = VK_FORMAT_D32_SFLOAT;
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		enum attachmentTags
		{
			colorAttTag,
			depthAttTag
		};

		enum subpassTags
		{
			mainSubpass
		};

		renderPassBuilder.addAttachment(std::move(colorAttachment), colorAttTag)
			.addAttachment(std::move(depth_attachment), depthAttTag);

		renderPassBuilder.initSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS, mainSubpass)
			.addColorAttachmentRef(colorAttTag, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			.setDepthStencilRef(depthAttTag)
			.endSubpass();

		// Use subpass dependencies for layout transitions
		VkSubpassDependency colorDependencyInit{};
		colorDependencyInit.srcSubpass = VK_SUBPASS_EXTERNAL;
		colorDependencyInit.dstSubpass = mainSubpass;
		colorDependencyInit.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		colorDependencyInit.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		colorDependencyInit.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		colorDependencyInit.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		colorDependencyInit.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkSubpassDependency depthDependency{};
		depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		depthDependency.dstSubpass = mainSubpass;
		depthDependency.srcStageMask =
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.srcAccessMask = 0;
		depthDependency.dstStageMask =
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkSubpassDependency colorDependencyFinal{};
		colorDependencyFinal.srcSubpass = mainSubpass;
		colorDependencyFinal.dstSubpass = VK_SUBPASS_EXTERNAL;
		colorDependencyFinal.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		colorDependencyFinal.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		colorDependencyFinal.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		colorDependencyFinal.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		colorDependencyFinal.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		renderPassBuilder.addSubpassDependency(std::move(colorDependencyInit))
			.addSubpassDependency(std::move(depthDependency))
			.addSubpassDependency(std::move(colorDependencyFinal));

		VkRenderPass renderPass = renderPassBuilder.build();

		return renderPass;
	}

	// GRAPHICS PIPELINE

	vkt::Rendering::Material createMaterialOpaque(vkt::Logical::Device* vktDevice, VkRenderPass& renderPass,
												  std::vector<VkDescriptorSetLayout> descriptorSetLayouts)
	{
		VkShaderModule vertShaderModule =
			vkt::Pipeline::createShaderModule(vktDevice, tools::paths::join({ SHADERS_DIR, "vert.spv" }));
		VkShaderModule fragShaderModule =
			vkt::Pipeline::createShaderModule(vktDevice, tools::paths::join({ SHADERS_DIR, "frag.spv" }));

		// ---------

		vkt::Rendering::Material material{};

		// shader stages

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{ vertShaderStageInfo, fragShaderStageInfo };

		// dynamic states
		std::vector<VkDynamicState> dynamicStates = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};

		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		// vertex state

		vkt::VertexInputDescription vertexInputDesc = vkt::Vertex::get_vertex_description();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputDesc.bindings.size());
		vertexInputInfo.pVertexBindingDescriptions = vertexInputDesc.bindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputDesc.attributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputDesc.attributes.data();

		// input assembly state
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// viewport
		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// depth stencil

		VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
		depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilInfo.pNext = nullptr;

		depthStencilInfo.depthTestEnable = VK_TRUE; // don't draw on top of other things
		depthStencilInfo.depthWriteEnable = VK_TRUE;
		depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
		depthStencilInfo.minDepthBounds = 0.0f; // Optional
		depthStencilInfo.maxDepthBounds = 1.0f; // Optional
		depthStencilInfo.stencilTestEnable = VK_FALSE;

		// rasterization
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f;		   // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f;	   // Optional

		// multisample
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f;			// Optional
		multisampling.pSampleMask = nullptr;			// Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE;		// Optional

		// color blend attachment
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		/*	Color blend Pseudocode:

			if (blendEnable) {
				finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor *
		   oldColor.rgb); finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor *
		   oldColor.a); } else { finalColor = newColor;
			}

			finalColor = finalColor & colorWriteMask;

		*/

		/* Simple alpha blending :

			finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
			finalColor.a = newAlpha.a;

		*/

		// color blend
		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		// push constants

		VkPushConstantRange push_constant{};
		push_constant.offset = 0;
		push_constant.size = sizeof(MeshPushConstants);
		push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		// pipeline layout

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &push_constant;

		VK_CHECK_RESULT(
			vkCreatePipelineLayout(vktDevice->vk(), &pipelineLayoutInfo, nullptr, &(material.pipelineLayout)));

		// pipeline cache

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(
			vkCreatePipelineCache(vktDevice->vk(), &pipelineCacheCreateInfo, nullptr, &(material.pipelineCache)));

		// pipeline

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		// shader stages
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		;
		pipelineInfo.pStages = shaderStages.data();
		// states
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencilInfo;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		// layout
		pipelineInfo.layout = material.pipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1;			  // Optional
		// renderpass
		pipelineInfo.renderPass = renderPass;
		// pipelineInfo.subpass = 0;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vktDevice->vk(), material.pipelineCache, 1, &pipelineInfo, nullptr,
												  &(material.pipeline)));

		vktDevice->pDeletionQueue->push_function([=]() {
			vkDestroyPipeline(vktDevice->vk(), material.pipeline, nullptr);
			vkDestroyPipelineCache(vktDevice->vk(), material.pipelineCache, nullptr);
			vkDestroyPipelineLayout(vktDevice->vk(), material.pipelineLayout, nullptr);
		});

		// ---------

		vkDestroyShaderModule(vktDevice->vk(), fragShaderModule, nullptr);
		vkDestroyShaderModule(vktDevice->vk(), vertShaderModule, nullptr);

		return material;
	}

	vkt::Rendering::Material createMaterialPP(vkt::Logical::Device* vktDevice, VkRenderPass& renderPass,
											  std::vector<VkDescriptorSetLayout> descriptorSetLayouts)
	{
		VkShaderModule vertShaderModule =
			vkt::Pipeline::createShaderModule(vktDevice, tools::paths::join({ SHADERS_DIR, "pp_vert.spv" }));
		VkShaderModule fragShaderModule =
			vkt::Pipeline::createShaderModule(vktDevice, tools::paths::join({ SHADERS_DIR, "pp_frag.spv" }));

		// ---------

		vkt::Rendering::Material material{};

		// shader stages

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{ vertShaderStageInfo, fragShaderStageInfo };

		// dynamic states
		std::vector<VkDynamicState> dynamicStates = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};

		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		// vertex state

		vkt::VertexInputDescription vertexInputDesc = vkt::Vertex::get_vertex_description();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputDesc.bindings.size());
		vertexInputInfo.pVertexBindingDescriptions = vertexInputDesc.bindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputDesc.attributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputDesc.attributes.data();

		// input assembly state
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// viewport
		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// depth stencil

		VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
		depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilInfo.pNext = nullptr;

		depthStencilInfo.depthTestEnable = VK_FALSE; // don't draw on top of other things
		depthStencilInfo.depthWriteEnable = VK_FALSE;
		depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
		depthStencilInfo.minDepthBounds = 0.0f; // Optional
		depthStencilInfo.maxDepthBounds = 1.0f; // Optional
		depthStencilInfo.stencilTestEnable = VK_FALSE;

		// rasterization
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f;		   // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f;	   // Optional

		// multisample
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f;			// Optional
		multisampling.pSampleMask = nullptr;			// Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE;		// Optional

		// color blend attachment
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		/*	Color blend Pseudocode:

			if (blendEnable) {
				finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor *
		   oldColor.rgb); finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor *
		   oldColor.a); } else { finalColor = newColor;
			}

			finalColor = finalColor & colorWriteMask;

		*/

		/* Simple alpha blending :

			finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
			finalColor.a = newAlpha.a;

		*/

		// color blend
		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		// push constants

		VkPushConstantRange push_constant{};
		push_constant.offset = 0;
		push_constant.size = sizeof(MeshPushConstants);
		push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		// pipeline layout

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &push_constant;

		VK_CHECK_RESULT(
			vkCreatePipelineLayout(vktDevice->vk(), &pipelineLayoutInfo, nullptr, &(material.pipelineLayout)));

		// pipeline cache

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(
			vkCreatePipelineCache(vktDevice->vk(), &pipelineCacheCreateInfo, nullptr, &(material.pipelineCache)));

		// pipeline

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		// shader stages
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		;
		pipelineInfo.pStages = shaderStages.data();
		// states
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencilInfo;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		// layout
		pipelineInfo.layout = material.pipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1;			  // Optional
		// renderpass
		pipelineInfo.renderPass = renderPass;
		// pipelineInfo.subpass = 0;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vktDevice->vk(), material.pipelineCache, 1, &pipelineInfo, nullptr,
												  &(material.pipeline)));

		vktDevice->pDeletionQueue->push_function([=]() {
			vkDestroyPipeline(vktDevice->vk(), material.pipeline, nullptr);
			vkDestroyPipelineCache(vktDevice->vk(), material.pipelineCache, nullptr);
			vkDestroyPipelineLayout(vktDevice->vk(), material.pipelineLayout, nullptr);
		});

		// ---------

		vkDestroyShaderModule(vktDevice->vk(), fragShaderModule, nullptr);
		vkDestroyShaderModule(vktDevice->vk(), vertShaderModule, nullptr);

		return material;
	}

	// MESH

	void loadTriangle(vkt::Rendering::Mesh* mesh)
	{
		// make the array 3 vertices long
		std::vector<vkt::Vertex> vertices(3);

		// vertex positions
		vertices[0].position = { 1.f, 1.f, 0.0f };
		vertices[1].position = { -1.f, 1.f, 0.0f };
		vertices[2].position = { 0.f, -1.f, 0.0f };

		vertices[0].color = { 0.f, 1.f, 0.0f };
		vertices[1].color = { .5f, 1.f, 0.0f };
		vertices[2].color = { 1.f, 0.f, 0.5f };

		// we don't care about the vertex normals

		mesh->setVertices(std::move(vertices));
	}

	void loadQuad(vkt::Rendering::Mesh* mesh)
	{
		std::vector<vkt::Vertex> vertices(6);

		vertices[0].position = { -1.f, 1.f, 0.0f };
		vertices[1].position = { -1.f, -1.f, 0.0f };
		vertices[2].position = { 1.f, -1.f, 0.0f };

		vertices[3].position = { 1.f, -1.f, 0.0f };
		vertices[4].position = { 1.f, 1.f, 0.0f };
		vertices[5].position = { -1.f, 1.f, 0.0f };

		vertices[0].uv = { 0.f, 0.f };
		vertices[1].uv = { 0.f, 1.f };
		vertices[2].uv = { 1.f, 1.f };

		vertices[3].uv = { 1.f, 1.f };
		vertices[4].uv = { 1.f, 0.f };
		vertices[5].uv = { 0.f, 0.f };

		mesh->setVertices(std::move(vertices));
	}

	void ReaShaderRenderer::_initVulkan()
	{
		// instance
		{
			myVkInstance = vkt::createVkInstance(vktMainDeletionQueue, "ReaShader Effect", "No Engine");
		}

		// device

		{
			vkt::Physical::DeviceSelector deviceSelector =
				vkt::Physical::DeviceSelector().enumerate(myVkInstance).removeUnsuitable(&isPhysicalDeviceSuitable);

			std::vector<VkPhysicalDeviceProperties> devicesProperties = deviceSelector.getProperties();

			 std::reverse(devicesProperties.begin(), devicesProperties.end());

			reaShaderProcessor->setRenderingDevicesList(devicesProperties);

			vkSuitablePhysicalDevices = deviceSelector.getDevices();

			 std::reverse(vkSuitablePhysicalDevices.begin(), vkSuitablePhysicalDevices.end());

			// choose stored rendering device index
			int renderingDeviceIndex = (int)(reaShaderProcessor->processor_rsParams[uRenderingDevice].value);
			if (renderingDeviceIndex >=
				vkSuitablePhysicalDevices.size()) // fall back to 0 if out of index (device list changed)
			{
				renderingDeviceIndex = 0;
				reaShaderProcessor->processor_rsParams[uRenderingDevice].value = 0;
			}

			_setUpDevice(renderingDeviceIndex);
		}
	}

	void ReaShaderRenderer::changeRenderingDevice(int renderingDeviceIndex)
	{
		halted = true;

		vktDevice->waitIdle();
		
		vktFrameResizedDeletionQueue.flush();
		vktPhysicalDeviceChangedDeletionQueue.flush();

		_setUpDevice(renderingDeviceIndex);

		halted = false;
	}

	void ReaShaderRenderer::_setUpDevice(int renderingDeviceIndex)
	{

		// device

		{
			vktPhysicalDevice = new vkt::Physical::Device(vktPhysicalDeviceChangedDeletionQueue, myVkInstance,
														  vkSuitablePhysicalDevices[renderingDeviceIndex]);

			vktDevice = new vkt::Logical::Device(vktPhysicalDeviceChangedDeletionQueue, vktPhysicalDevice);
		}

		// meshes

		vktPhysicalDeviceChangedDeletionQueue.push_function([&]() { meshes.clear(); });

		{
			vkt::Rendering::Mesh* quad = new vkt::Rendering::Mesh(vktDevice);
			loadQuad(quad);
			meshes.add(ids::meshes::quad, quad);
		}

		{
			vkt::Rendering::Mesh* reashader = new vkt::Rendering::Mesh(vktDevice);
			std::string path = tools::paths::join({ MESHES_DIR, "reashader.obj" });
			reashader->load_from_obj(path);
			meshes.add(ids::meshes::reashader, reashader);
		}

		{
			vkt::Rendering::Mesh* triangleMesh = new vkt::Rendering::Mesh(vktDevice);
			loadTriangle(triangleMesh);
			meshes.add(ids::meshes::triangle, triangleMesh);

			vkt::Rendering::Mesh* suzanne = new vkt::Rendering::Mesh(vktDevice);
			std::string path = tools::paths::join({ MESHES_DIR, "Suzanne.obj" });
			suzanne->load_from_obj(path);
			meshes.add(ids::meshes::suzanne, suzanne);
		}

		// textures

		vkSampler = vkt::textures::createSampler(vktDevice, VK_FILTER_LINEAR);

		vktPhysicalDeviceChangedDeletionQueue.push_function([&]() { textures.clear(); });

		{
			vkt::Images::AllocatedImage* texture = new vkt::Images::AllocatedImage(vktDevice);
			texture->createImage(tools::paths::join({ IMAGES_DIR, "reashader-logo-hr.png" }), VK_ACCESS_SHADER_READ_BIT,
								 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, texture->getFormat(), VK_IMAGE_ASPECT_COLOR_BIT);
			textures.add(ids::textures::logo, texture);
		}

		// buffers/descriptors

		// declare types and needs
		vktDescriptorPool = new vkt::Descriptors::DescriptorPool(vktDevice,
																 { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5 },
																   { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 5 },
																   { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 },
																   { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 } },
																 5);

		// bind sets

		// set 0
		frameData.globalSet =
			vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice)
				.bind(ids::descriptorBindings::gUb, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
				.bind(ids::descriptorBindings::gUbD, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
				.build();
		// set 1
		frameData.objectSet =
			vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice)
				.bind(ids::descriptorBindings::oSb, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
				.build();
		// set 2
		frameData.textureSet = vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice)
								   .bind(ids::descriptorBindings::tCis, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
										 VK_SHADER_STAGE_FRAGMENT_BIT)
								   .bind(ids::descriptorBindings::binding_sampled_frame,
										 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
								   .build();

		vktDescriptorPool->allocateDescriptorSets({ frameData.globalSet, frameData.objectSet, frameData.textureSet });

		// create buffers and images to bind

		frameData.cameraBuffer = new vkt::Buffers::AllocatedBuffer(vktDevice);
		frameData.cameraBuffer->allocate(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
										 VMA_MEMORY_USAGE_CPU_TO_GPU);

		frameData.sceneBuffer = new vkt::Buffers::AllocatedBuffer(vktDevice);
		frameData.sceneBuffer->allocate(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
										VMA_MEMORY_USAGE_CPU_TO_GPU);

		frameData.objectBuffer = new vkt::Buffers::AllocatedBuffer(vktDevice);
		frameData.objectBuffer->allocate(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
										 VMA_MEMORY_USAGE_CPU_TO_GPU);

		// renderpass
		vkRenderPass = createRenderPass(vktDevice);
		// initialize render targets
		_createRenderTargets();

		// write resources pointers to descriptor sets

		vkt::Descriptors::DescriptorSetWriter(vktDevice)
			.selectDescriptorSet(frameData.globalSet)
			.selectBinding(ids::descriptorBindings::gUb)
			.registerWriteBuffer(frameData.cameraBuffer, sizeof(GPUCameraData), 0)
			.selectBinding(ids::descriptorBindings::gUbD)
			.registerWriteBuffer(frameData.sceneBuffer, sizeof(GPUSceneData), 0)

			.selectDescriptorSet(frameData.objectSet)
			.selectBinding(ids::descriptorBindings::oSb)
			.registerWriteBuffer(frameData.objectBuffer, sizeof(GPUObjectData) * MAX_OBJECTS, 0)

			.selectDescriptorSet(frameData.textureSet)
			.selectBinding(ids::descriptorBindings::tCis)
			.registerWriteImage(*textures.get(ids::textures::logo), vkSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.selectBinding(ids::descriptorBindings::binding_sampled_frame)
			.registerWriteImage(vktPostProcessSource, vkSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)

			.writeRegistered();

		// pipeline

		// Materials

		vktPhysicalDeviceChangedDeletionQueue.push_function([&]() { materials.clear(); });

		// post process
		{
			vkt::Rendering::Material material_post_process = createMaterialPP(
				vktDevice, vkRenderPass,
				{ frameData.globalSet.layout, frameData.objectSet.layout, frameData.textureSet.layout });
			material_post_process.textureSet = &frameData.textureSet;
			materials.add(ids::materials::post_process, std::move(material_post_process));
		}

		// opaque
		{
			vkt::Rendering::Material material_opaque = createMaterialOpaque(
				vktDevice, vkRenderPass,
				{ frameData.globalSet.layout, frameData.objectSet.layout, frameData.textureSet.layout });
			material_opaque.textureSet = &frameData.textureSet;
			materials.add(ids::materials::opaque, std::move(material_opaque));
		}

		// render objects

		vktPhysicalDeviceChangedDeletionQueue.push_function([&]() { renderObjects.clear();	});

		{
			vkt::Rendering::RenderObject pp{};
			pp.mesh = *meshes.get(ids::meshes::quad);
			pp.material = materials.get(ids::materials::post_process);
			renderObjects.push_back(std::move(pp));
		}

		{
			vkt::Rendering::RenderObject reashader{};
			reashader.mesh = *meshes.get(ids::meshes::reashader);
			reashader.material = materials.get(ids::materials::opaque);
			reashader.localTransformMatrix = glm::rotate(glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));

			renderObjects.push_back(std::move(reashader));

			/*	::RenderObject monkey{};
				monkey.mesh = *meshes.get(ids::meshes::suzanne);
				monkey.material = materials.get(ids::materials::opaque);
				monkey.localTransformMatrix = glm::mat4{ 1.0f };

				renderObjects.push_back(std::move(monkey));*/

			/*::RenderObject triangle{};
			triangle.mesh = *meshes.get(ids::meshes::triangle);
			triangle.material = materials.get(ids::materials::opaque);
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(5, 0, 5));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
			triangle.localTransformMatrix = translation * scale;
			renderObjects.push_back(std::move(triangle));*/
		}

		// command buffers
		{
			vktDevice->getGraphicsCommandPool()->createCommandBuffers(
				{ ids::commandBuffers::draw, ids::commandBuffers::transfer },
				{ &vkDrawCommandBuffer, &vkTransferCommandBuffer });
		}

		vkInFlightFence = vkt::sync::createFence(vktDevice, false);
		vkRenderFinishedSemaphore = vkt::sync::createSemaphore(vktDevice);
		vkImageAvailableSemaphore = vkt::sync::createSemaphore(vktDevice);

		// check init properties

		// Check blit support for source and destination
		if (!vktPhysicalDevice->supportsBlit())
		{
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!"
					  << std::endl;
		}
	}

	void ReaShaderRenderer::_createRenderTargets()
	{
		auto frameResizedDeletionQueue = &vktFrameResizedDeletionQueue;

		// render target

		vktColorAttachment = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		vktColorAttachment->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vktColorAttachment->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

		// depth buffer

		vktDepthAttachment = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		vktDepthAttachment->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vktDepthAttachment->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

		// frame transfer

		vktFrameTransfer = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		vktFrameTransfer->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // both src and dst for copy cmds
			VMA_MEMORY_USAGE_GPU_TO_CPU, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

		// post process source

		vktPostProcessSource = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		vktPostProcessSource->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, NULL);
		vktPostProcessSource->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM,
											  VK_IMAGE_ASPECT_COLOR_BIT);

		// framebuffer

		vkFramebuffer =
			vkt::Pipeline::createFramebuffer(vktDevice, frameResizedDeletionQueue, vkRenderPass, { FRAME_W, FRAME_H },
											 { vktColorAttachment, vktDepthAttachment });
	}

	void ReaShaderRenderer::_cleanupVulkan()
	{
		vktDevice->waitIdle();
		// flush deletion queues in reverse order
		vktFrameResizedDeletionQueue.flush();
		vktPhysicalDeviceChangedDeletionQueue.flush();
		vktMainDeletionQueue.flush();
	}

} // namespace ReaShader