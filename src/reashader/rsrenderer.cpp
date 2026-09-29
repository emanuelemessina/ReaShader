/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "rsrenderer.h"
#include "reashaderplugin.h"
#include "rsparams/rsparams.h"
#include "tools/exceptions.h"
#include "tools/logging.h"
#include "tools/paths.h"

#include <queue>

#define MAX_OBJECTS 100
#define BYTES_PER_PIXEL 4 // REAPER 'RGBA' frames: 4 bytes per pixel

#include "vkt/vktcommandpool.h"
#include "vkt/vktcommands.h"
#include "vkt/vktpipeline.h"
#include "vkt/vktqueue.h"
#include "vkt/vkttextures.h"

namespace ReaShader
{
	using ShaderVariable = vkt::Pipeline::Shader::Variable;
	using ShaderPushConstants = vkt::Pipeline::Shader::PushConstants;
	using ShaderUniformBuffer = vkt::Pipeline::Shader::UniformBuffer;
	using ShaderSampledImage = vkt::Pipeline::Shader::SampledImage;

	namespace
	{
		// <plugin dir>/assets/<subdir>/<file>, as a narrow path for the file-loading APIs
		std::string assetPath(const char* subdir, const char* file)
		{
			return (tools::paths::assetsDir() / subdir / file).string();
		}
	} // namespace

	ReaShaderRenderer::ReaShaderRenderer(ReaShaderPlugin* reaShaderPlugin)
		: reaShaderPlugin(reaShaderPlugin)
	{
	}

	void ReaShaderRenderer::_initVulkanGuarded()
	{
		WRAP_LOW_LEVEL_FAULTS(_initVulkan();, "ReaShaderRenderer", "Fatal Error", "Caught during initVulkan")
	}

	void ReaShaderRenderer::init()
	{
		std::lock_guard<std::mutex> lock(frameMutex);

		exceptionOnInitialize = false;
		frameFailed = false;

		// init vulkan

		try
		{
			_initVulkanGuarded();
		}
		catch (const std::exception& e)
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
		std::lock_guard<std::mutex> lock(frameMutex);

		// clean up vulkan

		if (exceptionOnInitialize)
			return;

		try
		{
			_cleanupVulkan();
		}
		catch (const std::exception& e)
		{
			LOG(e, toFile | toConsole | toBox, "ReaShaderRenderer", "Exception: ", "ReaShader crashed...");
		}
	}

	bool ReaShaderRenderer::renderFrame(int w, int h, int* inputBits, double pushConstants[], int* outputBits)
	{
		std::unique_lock<std::mutex> lock(frameMutex, std::try_to_lock);
		if (!lock.owns_lock() || exceptionOnInitialize || frameFailed || halted || !vktDevice)
			return false;

		// this runs on REAPER's video thread: an exception escaping into REAPER is an unhandled
		// exception there, which aborts the whole process (0x40000015 inside reaper.exe)
		try
		{
			checkFrameSize(w, h);
			loadBitsToImage(inputBits);
			drawFrame(pushConstants);
			transferFrame(outputBits);
			return true;
		}
		catch (const std::exception& e)
		{
			frameFailed = true;
			LOG(e, toFile | toConsole, "ReaShaderRenderer", "Frame rendering failed, passing video through",
				"Rendering disabled until the plugin is re-activated");
		}
		catch (...)
		{
			frameFailed = true;
			LOG(EXCEPTION, toFile | toConsole, "ReaShaderRenderer", "Frame rendering failed, passing video through",
				"Unknown exception -- rendering disabled until the plugin is re-activated");
		}
		return false;
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
			deletionQueues.vktFrameResized.flush();
			createRenderTargets();

			// call listener
			if (listener)
				listener();
		}
	}

	struct defaultIds
	{
		enum descriptorBindings
		{
			global_uniform_buffer = 0,
			global_uniform_buffer_dynamic,
			object_storage_buffer = 0,
			texture_combined_image_sampler = 0,
			sampled_frame
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
	} defaultIds;

	// DRAW

	struct DefaultPushConstants
	{
		glm::int32 objectId;
		glm::float32 videoParam;
	};

	struct RenderObjectData
	{
		glm::mat4 finalModelMatrix;
	};

	void ReaShaderRenderer::updateVirtualScene(double pushConstants[])
	{
		double proj_time = pushConstants[0];
		double frameNumber = proj_time * pushConstants[1]; // proj_time * frate

		// camera position
		glm::vec3 camPos = { 0.f, 0.f, -5.f };
		glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
		// camera projection
		glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.0f);
		// projection[1][1] *= -1;
						
		virtualScene.camData.proj = projection;
		virtualScene.camData.view = view;
		virtualScene.camData.viewproj = projection * view;

		virtualScene.cameraBuffer->putData(&virtualScene.camData, sizeof(VirtualScene::VirtualCameraData));

		virtualScene.environmentBuffer->putData(&virtualScene.envData, sizeof(VirtualScene::VirtualEnvironmentData));
	}

	void ReaShaderRenderer::drawFrame(double pushConstants[])
	{
		if (halted)
			return;

		vkt::CommandPool* commandPool = vktDevice->getGraphicsCommandPool();
		VkCommandBuffer commandBuffer = commandBuffers.vkDraw;
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

		// virtual scene data
		updateVirtualScene(pushConstants);

		// update objects
		double proj_time = pushConstants[0];
		double frameNumber = proj_time * pushConstants[1]; // proj_time * frate
		glm::mat4 modelTransform = glm::rotate(glm::mat4{ 1.0f }, (float)glm::radians(frameNumber * 1.f),
											   glm::vec3(0.1f * sin(proj_time), 1, 0.05f * cos(proj_time)));

		void* data;
		virtualScene.objectBuffer->map(&data);

		// render objects

		RenderObjectData* objectSSBO = (RenderObjectData*)data;

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
				object.material->cmdBindDescriptors(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);
			}

			// set push constants

			// model rotation
			DefaultPushConstants constants{};
			constants.objectId = i;
			constants.videoParam = pushConstants[2];

			object.material->cmdPushConstants(commandBuffer, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
											  &constants, 0);

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
		virtualScene.objectBuffer->unmap();

		// end render pass

		vkCmdEndRenderPass(commandBuffer);

		// submit ( end command buffer )

		commandPool->submit(commandBuffer, VK_NULL_HANDLE, syncObjects.vkRenderFinishedSemaphore, syncObjects.vkImageAvailableSemaphore);
	}

	void ReaShaderRenderer::transferFrame(int*& destBuffer)
	{
		if (halted)
			return;

		// init command buffer

		VkCommandBuffer commandBuffer = commandBuffers.vkTransfer;
		vkt::CommandPool* commandPool = vktDevice->getGraphicsCommandPool();

		VK_CHECK_RESULT(vkQueueWaitIdle(vktDevice->getGraphicsQueue()->vk()));

		commandPool->restartCommandBuffer(commandBuffer);

		// Transition destination image to transfer destination layout

		vkt::commands::insertImageMemoryBarrier(
			commandBuffer, renderTargets.vktFrameTransfer->getImage(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
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
			vkCmdBlitImage(commandBuffer, renderTargets.vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   renderTargets.vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlitRegion,
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
			vkCmdCopyImage(commandBuffer, renderTargets.vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   renderTargets.vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory
		// later on
		vkt::commands::insertImageMemoryBarrier(
			commandBuffer, renderTargets.vktFrameTransfer->getImage(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit queue (wait for draw frame)

		commandPool->submit(commandBuffer, syncObjects.vkInFlightFence, VK_NULL_HANDLE,
							syncObjects.vkRenderFinishedSemaphore);

		// wait fence since now we are on cpu

		VK_CHECK_RESULT(vkWaitForFences(vktDevice->vk(), 1, &syncObjects.vkInFlightFence, VK_TRUE, UINT64_MAX))
		VK_CHECK_RESULT(vkResetFences(vktDevice->vk(), 1, &syncObjects.vkInFlightFence))

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;

		vkGetImageSubresourceLayout(vktDevice->vk(), renderTargets.vktFrameTransfer->getImage(), &subResource, &subResourceLayout);

		// dest image is already mapped
		memcpy((void*)destBuffer,
			   (void*)(reinterpret_cast<uintptr_t>((renderTargets.vktFrameTransfer->getAllocationInfo()).pMappedData) +
					   subResourceLayout.offset),
			   BYTES_PER_PIXEL * FRAME_W * FRAME_H);
	}

	// load vf bits to color attachment
	void ReaShaderRenderer::loadBitsToImage(int* srcBuffer)
	{
		if (halted)
			return;

		vkt::CommandPool* commandPool = vktDevice->getGraphicsCommandPool();
		vkt::Queue* queue = vktDevice->getGraphicsQueue();
		VkCommandBuffer commandBuffer = commandBuffers.vkTransfer;

		VK_CHECK_RESULT(vkQueueWaitIdle(queue->vk()))

		commandPool->restartCommandBuffer(commandBuffers.vkTransfer);

		vkt::commands::transferRawBufferToImage(vktDevice, commandBuffer, srcBuffer, renderTargets.vktFrameTransfer,
												BYTES_PER_PIXEL * FRAME_W * FRAME_H);

		// retransition frametransfer to src copy optimal

		vkt::commands::insertImageMemoryBarrier(commandBuffers.vkTransfer, renderTargets.vktFrameTransfer->getImage(),
												VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
												VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
												VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
												VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// color attachment goes dst optimal

		vkt::commands::insertImageMemoryBarrier(
			commandBuffers.vkTransfer, renderTargets.vktColorAttachment->getImage(), 0, VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// as well as post process source

		vkt::commands::insertImageMemoryBarrier(
			commandBuffers.vkTransfer, renderTargets.vktPostProcessSource->getImage(), 0, VK_ACCESS_MEMORY_READ_BIT,
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

		vkCmdCopyImage(commandBuffers.vkTransfer, renderTargets.vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   renderTargets.vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);

		// and to post process source

		vkCmdCopyImage(commandBuffers.vkTransfer, renderTargets.vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   renderTargets.vktPostProcessSource->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);

		// retransition post process source to shader read optimal

		vkt::commands::insertImageMemoryBarrier(commandBuffers.vkTransfer, renderTargets.vktPostProcessSource->getImage(), 0,
												VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
												VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
												VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
												VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		commandPool->submit(commandBuffers.vkTransfer, syncObjects.vkInFlightFence,
							syncObjects.vkImageAvailableSemaphore, VK_NULL_HANDLE);

		// write descriptor for post process source

		VK_CHECK_RESULT(vkWaitForFences(vktDevice->vk(), 1, &syncObjects.vkInFlightFence, VK_TRUE, UINT64_MAX))
		VK_CHECK_RESULT(vkResetFences(vktDevice->vk(), 1, &syncObjects.vkInFlightFence))

		vkt::Descriptors::DescriptorSetWriter(vktDevice)
			.selectDescriptorSet(virtualScene.textureSet)
			.selectBinding(defaultIds::descriptorBindings::sampled_frame)
			.registerWriteImage(renderTargets.vktPostProcessSource, vkSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
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
		// deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
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
			vkt::Pipeline::createShaderModule(vktDevice, assetPath("shaders", "vert.spv"));
		VkShaderModule fragShaderModule =
			vkt::Pipeline::createShaderModule(vktDevice, assetPath("shaders", "frag.spv"));

		// ---------
		
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
		// TODO: REFLECTION
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
		// TODO: REFLECTION
		VkPushConstantRange push_constant{};
		push_constant.offset = 0;
		push_constant.size = sizeof(DefaultPushConstants);
		push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		vkt::Rendering::Material material =
			vkt::Pipeline::MaterialBuilder(vktDevice)
				.beginPipelineLayout()
				.setPushConstants(push_constant)
				.setDescriptors(descriptorSetLayouts)
				.endPipelineLayout()
				.beginPipeline()
				.setShaderStages({ vertShaderStageInfo, fragShaderStageInfo })
				.setVertexState(vertexInputInfo)
				.setInputAssembly(inputAssembly)
				.setViewPortState(viewportState)
				.setRasterizer(rasterizer)
				.setMultisampling(multisampling)
				.setDepthStencil(depthStencilInfo)
				.setColorBlending(colorBlending)
				.setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
				.endPipeline(renderPass)
				.build();

		// ---------

		vkDestroyShaderModule(vktDevice->vk(), fragShaderModule, nullptr);
		vkDestroyShaderModule(vktDevice->vk(), vertShaderModule, nullptr);

		return material;
	}

	vkt::Rendering::Material createMaterialPP(vkt::Logical::Device* vktDevice, VkRenderPass& renderPass,
											  std::vector<VkDescriptorSetLayout> descriptorSetLayouts)
	{
		VkShaderModule vertShaderModule = vkt::Pipeline::createShaderModule(
			vktDevice, assetPath("shaders", "pp_vert.spv"));
		std::string compilationMessage;
		VkShaderModule fragShaderModule = vkt::Pipeline::createShaderModule(
			vktDevice, EShLangFragment, assetPath("shaders", "pp_frag.glsl"), compilationMessage);

		// ---------

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

		// vertex state
		vkt::VertexInputDescription vertexInputDesc = vkt::Vertex::get_vertex_description();
		VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkt::Vertex::get_pipeline_input_state(vertexInputDesc);

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
		push_constant.size = sizeof(DefaultPushConstants);
		push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		
		vkt::Rendering::Material material =
			vkt::Pipeline::MaterialBuilder(vktDevice)
				.beginPipelineLayout()
				.setPushConstants(push_constant)
				.setDescriptors(descriptorSetLayouts)
				.endPipelineLayout()
				.beginPipeline()
				.setShaderStages({ vertShaderStageInfo, fragShaderStageInfo })
				.setVertexState(vertexInputInfo)
				.setInputAssembly(inputAssembly)
				.setViewPortState(viewportState)
				.setRasterizer(rasterizer)
				.setMultisampling(multisampling)
				.setDepthStencil(depthStencilInfo)
				.setColorBlending(colorBlending)
				.setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
				.endPipeline(renderPass)
				.build();

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
			myVkInstance = vkt::createVkInstance(deletionQueues.vktMain, "ReaShader Effect", "No Engine");
		}

		// device

		{
			vkt::Physical::DeviceSelector deviceSelector =
				vkt::Physical::DeviceSelector().enumerate(myVkInstance).removeUnsuitable(&isPhysicalDeviceSuitable);

			std::vector<VkPhysicalDeviceProperties> devicesProperties = deviceSelector.getProperties();

			std::reverse(devicesProperties.begin(), devicesProperties.end());

			std::vector<std::string> deviceNames;
			for (auto& props : devicesProperties)
				deviceNames.push_back(props.deviceName);
			reaShaderPlugin->setRenderingDevicesList(deviceNames);

			vkSuitablePhysicalDevices = deviceSelector.getDevices();

			std::reverse(vkSuitablePhysicalDevices.begin(), vkSuitablePhysicalDevices.end());

			// choose stored rendering device index
			int renderingDeviceIndex = (int)reaShaderPlugin->getRenderingDeviceIndex();
			if (renderingDeviceIndex >=
				vkSuitablePhysicalDevices.size()) // fall back to 0 if out of index (device list changed)
			{
				renderingDeviceIndex = 0;
				reaShaderPlugin->setRenderingDeviceIndex(0);
			}

			setUpDevice(renderingDeviceIndex);
		}
	}

	void ReaShaderRenderer::changeRenderingDevice(int renderingDeviceIndex)
	{
		std::lock_guard<std::mutex> lock(frameMutex);

		if (!vktDevice) // renderer not initialized yet (e.g. a stray/early web UI message) -- nothing to switch from
			return;

		halted = true;

		vktDevice->waitIdle();

		deletionQueues.vktFrameResized.flush();
		deletionQueues.vktPhysicalDeviceChanged.flush();

		setUpDevice(renderingDeviceIndex);

		frameFailed = false;
		halted = false;
	}

	void ReaShaderRenderer::changeCustomShader(std::vector<char>&& glsl,
											   std::function<void(std::string&& msg)> onStatus,
											   std::function<void(std::string&& msg)> onError,
											   std::function<void(void)> onSuccess)
	{
		onStatus("Compiling...");

		// compile
		std::vector<uint32_t> spv;
		std::string msg;
		if (!vkt::Pipeline::compile_glsl_to_spirv(std::move(glsl), EShLangFragment, spv, msg))
		{
			onError(std::move(msg));			
			return;
		}

		// reflect

		onStatus("Reflecting...");

		halted = true;

		spirv_cross::Compiler compiler(spv.data(), spv.size());
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		std::vector<std::unique_ptr<ShaderVariable>> variables;

		// vst param system
		std::vector<std::unique_ptr<Parameters::IParameter>> newParameters;
		int currentParamId = (int)reaShaderPlugin->rsParamsCount();

		// descriptors
		auto dslb = vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice);
		auto dsw = vkt::Descriptors::DescriptorSetWriter(vktDevice);
		int currentBinding = 0;
		std::queue<std::function<void()>> binders;

		// TODO: check pushconstants names for dedicated variables like projTime
		// check size of declared ubo (if> max ubo size throw err)

		// eventually remove variables vectors

		// push constants

		for (int i = 0; i < resources.push_constant_buffers.size(); i++)
		{
			// parse
			auto& resource = resources.push_constant_buffers[i]; 
			variables.push_back(std::make_unique<ShaderPushConstants>(compiler, resource));

			auto& spc = (std::unique_ptr<ShaderPushConstants>&) variables.back();

			// add param (allocate)
			for (int i = 0; i < spc->members.size(); i++)
			{
				auto& m = spc->members[i];

				for (int x = 0; x < m->cols; x++)
				{
					for (int y = 0; y < m->rows; y++)
					{
						std::unique_ptr<Parameters::ShaderParameter> p = std::make_unique<Parameters::ShaderParameter>(
							currentParamId, std::format("{} ({},{})", m->name, x, y), Parameters::Group::Shader,
							"pushConstants", m->name, "");
						newParameters.push_back(std::move(p));
						currentParamId++;
					}
				}
			}
		}	

		// ubo

		for (int i = 0; i < resources.uniform_buffers.size(); i++)
		{
			// parse
			auto& resource = resources.uniform_buffers[i];
			variables.push_back(std::make_unique<ShaderUniformBuffer>(compiler, resource));

			// allocate

			auto& buff = (std::unique_ptr<ShaderUniformBuffer>&) variables.back();
			uint32_t size = buff->size;

			auto allocBuff = new vkt::Buffers::AllocatedBuffer(vktDevice);
			postProcessData.buffers.push_back(allocBuff);
			allocBuff->allocate(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		
			// bind

			dslb.bind(currentBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);

			binders.push([&dsw, currentBinding, size, allocBuff, this]() {
				dsw.selectBinding(currentBinding)
					.registerWriteBuffer(allocBuff, size, 0);
			});

			currentBinding++;

			// add param
			for (int i = 0; i < buff->members.size(); i++)
			{
				auto& m = buff->members[i];

				for (int x = 0; x < m->cols; x++)
				{
					for (int y = 0; y < m->rows; y++)
					{
						std::unique_ptr<Parameters::ShaderParameter> p = std::make_unique<Parameters::ShaderParameter>(
							currentParamId, std::format("{} ({},{})", m->name, x,y), Parameters::Group::Shader, buff->name, m->name,
							"");
						newParameters.push_back(std::move(p));
						currentParamId++;
					}
				}
			}
			
		}

		for (auto& resource : resources.sampled_images)
		{
			// parse
			variables.push_back(std::make_unique<ShaderSampledImage>(compiler, resource));		

			// allocate
			vkt::Images::AllocatedImage* texture = new vkt::Images::AllocatedImage(vktDevice);
			postProcessData.textures.push_back(texture);

			// wait for actual texture file to bind
			/*

			// create texture from file

			// replace with create image from bytes
			texture->createImage(assetPath("images", "reashader-logo-hr.png"), VK_ACCESS_SHADER_READ_BIT,
								 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, texture->getFormat(), VK_IMAGE_ASPECT_COLOR_BIT);
			
			// bind

			dslb.bind(defaultIds::descriptorBindings::sampled_frame, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							 VK_SHADER_STAGE_FRAGMENT_BIT);

			binders.push([&dsw, currentBinding, texture, this]() {
				dsw.selectBinding(currentBinding)
						  .registerWriteImage(texture, vkSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			});

			currentBinding++;
			*/

			// add param
			auto& i = (std::unique_ptr<ShaderSampledImage>&) variables.back();
			auto p = std::make_unique<Parameters::String>(currentParamId, i->name, Parameters::Group::Shader);
			newParameters.push_back(std::move(p));
		}

		// build descriptors layout
		postProcessData.globalSet = dslb.build();

		// allocate descriptor set on pool
		vktDescriptorPool->allocateDescriptorSets({postProcessData.globalSet});

		// write binded to layout
		
		dsw.selectDescriptorSet(postProcessData.globalSet);

		while (!binders.empty())
		{
			auto bind = binders.front();
			bind();
			binders.pop();
		}

		dsw.writeRegistered();

		// -> init vulkan and device
		// -> create the opaque material
		// -> wait for the dedicated custom pp shader in the renderer before doing anything
		// -> ..
		// -> async create the shader module
		// compile shader
		// save spirv to file in shadersm folder
		// set custom shader filename
		// reflect, bind resources and register as new params
		// -> refresh the new params in the ui
		// -> receive new params from ui, allocate and unlock the pp material
		// pp material will be drawn
	
		halted = false;

		onStatus("Saving...");

		// add parameters

		for (auto& ptr : newParameters)
			reaShaderPlugin->addRendererParam(ptr);

		// wait for param population

		// register cmb bind in the material
		
		onStatus("Finished.");

		onSuccess();
	}

	void ReaShaderRenderer::setUpDevice(int renderingDeviceIndex)
	{
		// device

		{
			vktPhysicalDevice = new vkt::Physical::Device(deletionQueues.vktPhysicalDeviceChanged, myVkInstance,
														  vkSuitablePhysicalDevices[renderingDeviceIndex]);

			vktDevice = new vkt::Logical::Device(deletionQueues.vktPhysicalDeviceChanged, vktPhysicalDevice);
		}

		// command buffers
		{
			vktDevice->getGraphicsCommandPool()->createCommandBuffers(
				{ defaultIds::commandBuffers::draw, defaultIds::commandBuffers::transfer },
				{ &commandBuffers.vkDraw, &commandBuffers.vkTransfer });
		}

		syncObjects.vkInFlightFence = vkt::sync::createFence(vktDevice, false);
		syncObjects.vkRenderFinishedSemaphore = vkt::sync::createSemaphore(vktDevice);
		syncObjects.vkImageAvailableSemaphore = vkt::sync::createSemaphore(vktDevice);

		// check init properties

		// Check blit support for source and destination
		if (!vktPhysicalDevice->supportsBlit())
		{
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!"
					  << std::endl;
		}

		_setupRendering();
	}

	void ReaShaderRenderer::createRenderTargets()
	{
		auto frameResizedDeletionQueue = &deletionQueues.vktFrameResized;

		// render target

		renderTargets.vktColorAttachment = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		renderTargets.vktColorAttachment->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		renderTargets.vktColorAttachment->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

		// depth buffer

		renderTargets.vktDepthAttachment = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		renderTargets.vktDepthAttachment->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		renderTargets.vktDepthAttachment->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

		// frame transfer

		renderTargets.vktFrameTransfer = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		renderTargets.vktFrameTransfer->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // both src and dst for copy cmds
			VMA_MEMORY_USAGE_GPU_TO_CPU, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

		// post process source

		renderTargets.vktPostProcessSource = new vkt::Images::AllocatedImage(vktDevice, frameResizedDeletionQueue);
		renderTargets.vktPostProcessSource->createImage(
			{ FRAME_W, FRAME_H }, VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, NULL);
		renderTargets.vktPostProcessSource->createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM,
											  VK_IMAGE_ASPECT_COLOR_BIT);

		// framebuffer

		vkFramebuffer =
			vkt::Pipeline::createFramebuffer(vktDevice, frameResizedDeletionQueue, vkRenderPass, { FRAME_W, FRAME_H },
											 { renderTargets.vktColorAttachment, renderTargets.vktDepthAttachment });
	}

	void ReaShaderRenderer::_createDefaultMeshes()
	{
		deletionQueues.vktPhysicalDeviceChanged.push_function([&]() { meshes.clear(); });

		{
			vkt::Rendering::Mesh* quad = new vkt::Rendering::Mesh(vktDevice);
			loadQuad(quad);
			meshes.add(defaultIds::meshes::quad, quad);
		}

		{
			vkt::Rendering::Mesh* reashader = new vkt::Rendering::Mesh(vktDevice);
			std::string path = assetPath("meshes", "reashader.obj");
			reashader->load_from_obj(path);
			meshes.add(defaultIds::meshes::reashader, reashader);
		}

	}
	void ReaShaderRenderer::_createDefaultTextures()
	{
		vkSampler = vkt::textures::createSampler(vktDevice, VK_FILTER_LINEAR);

		deletionQueues.vktPhysicalDeviceChanged.push_function([&]() { textures.clear(); });

		{
			vkt::Images::AllocatedImage* texture = new vkt::Images::AllocatedImage(vktDevice);
			texture->createImage(assetPath("images", "reashader-logo-hr.png"), VK_ACCESS_SHADER_READ_BIT,
								 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, texture->getFormat(), VK_IMAGE_ASPECT_COLOR_BIT);
			textures.add(defaultIds::textures::logo, texture);
		}
	}

	void ReaShaderRenderer::_setupRendering()
	{
		// renderpass
		vkRenderPass = createRenderPass(vktDevice);

		// initialize render targets
		createRenderTargets();

		// create default resources

		// meshes
		_createDefaultMeshes();
		// textures
		_createDefaultTextures();

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
		virtualScene.globalSet = vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice)
										 .bind(defaultIds::descriptorBindings::global_uniform_buffer,
											   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
										 .bind(defaultIds::descriptorBindings::global_uniform_buffer_dynamic,
											   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
											   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
										 .build();
		// set 1
		virtualScene.objectSet = vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice)
										 .bind(defaultIds::descriptorBindings::object_storage_buffer,
											   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
										 .build();
		// set 2
		virtualScene.textureSet = vkt::Descriptors::DescriptorSetLayoutBuilder(vktDevice)
										  .bind(defaultIds::descriptorBindings::texture_combined_image_sampler,
												VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
										  .bind(defaultIds::descriptorBindings::sampled_frame,
												VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
										  .build();

		vktDescriptorPool->allocateDescriptorSets(
			{ virtualScene.globalSet, virtualScene.objectSet, virtualScene.textureSet });

		// create buffers and images to bind

		virtualScene.cameraBuffer = new vkt::Buffers::AllocatedBuffer(vktDevice);
		virtualScene.cameraBuffer->allocate(sizeof(VirtualScene::VirtualCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
												VMA_MEMORY_USAGE_CPU_TO_GPU);

		virtualScene.environmentBuffer = new vkt::Buffers::AllocatedBuffer(vktDevice);
		virtualScene.environmentBuffer->allocate(sizeof(VirtualScene::VirtualEnvironmentData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
											   VMA_MEMORY_USAGE_CPU_TO_GPU);

		virtualScene.objectBuffer = new vkt::Buffers::AllocatedBuffer(vktDevice);
		virtualScene.objectBuffer->allocate(sizeof(RenderObjectData) * MAX_OBJECTS,
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		// write resources pointers to descriptor sets

		vkt::Descriptors::DescriptorSetWriter(vktDevice)
			.selectDescriptorSet(virtualScene.globalSet)
			.selectBinding(defaultIds::descriptorBindings::global_uniform_buffer)
			.registerWriteBuffer(virtualScene.cameraBuffer, sizeof(VirtualScene::VirtualCameraData), 0)
			.selectBinding(defaultIds::descriptorBindings::global_uniform_buffer_dynamic)
			.registerWriteBuffer(virtualScene.environmentBuffer, sizeof(VirtualScene::VirtualEnvironmentData), 0)

			.selectDescriptorSet(virtualScene.objectSet)
			.selectBinding(defaultIds::descriptorBindings::object_storage_buffer)
			.registerWriteBuffer(virtualScene.objectBuffer, sizeof(RenderObjectData) * MAX_OBJECTS, 0)

			.selectDescriptorSet(virtualScene.textureSet)
			.selectBinding(defaultIds::descriptorBindings::texture_combined_image_sampler)
			.registerWriteImage(*textures.get(defaultIds::textures::logo), vkSampler,
								VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.selectBinding(defaultIds::descriptorBindings::sampled_frame)
			.registerWriteImage(renderTargets.vktPostProcessSource, vkSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)

			.writeRegistered();

		// Materials

		// post process

		// custom shader check, allocate saved params


		{
			vkt::Rendering::Material material_post_process =
				createMaterialPP(vktDevice, vkRenderPass, { virtualScene.textureSet.layout });

			material_post_process.registerBindDescriptorSets(0, 1, &(virtualScene.textureSet.set), 0, nullptr);

			materials.add(defaultIds::materials::post_process, std::move(material_post_process));
		}

		std::vector<uint32_t> dynamicOffsets = {
			0
		}; // offset for each binding to a dynamic descriptor, in order of binding registration

		// opaque
		{
			vkt::Rendering::Material material_opaque =
				createMaterialOpaque(vktDevice, vkRenderPass,
									 { virtualScene.globalSet.layout, virtualScene.objectSet.layout,
									   virtualScene.textureSet.layout });

			material_opaque
				.registerBindDescriptorSets(0, 1, &virtualScene.globalSet.set,
											static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data())
				.registerBindDescriptorSets(1, 1, &virtualScene.objectSet.set, 0, nullptr)
				.registerBindDescriptorSets(2, 1, &(virtualScene.textureSet.set), 0, nullptr);

			materials.add(defaultIds::materials::opaque, std::move(material_opaque));
		}

		deletionQueues.vktPhysicalDeviceChanged.push_function([&]() { materials.clear(); });

		// render objects

		deletionQueues.vktPhysicalDeviceChanged.push_function([&]() { renderObjects.clear(); });

		{
			vkt::Rendering::RenderObject pp{};
			pp.mesh = *meshes.get(defaultIds::meshes::quad);
			pp.material = materials.get(defaultIds::materials::post_process);
			renderObjects.push_back(std::move(pp));
		}

		{
			vkt::Rendering::RenderObject reashader{};
			reashader.mesh = *meshes.get(defaultIds::meshes::reashader);
			reashader.material = materials.get(defaultIds::materials::opaque);
			reashader.localTransformMatrix = glm::rotate(glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));

			renderObjects.push_back(std::move(reashader));

			/*	::RenderObject monkey{};
				monkey.mesh = *meshes.get(defaultIds::meshes::suzanne);
				monkey.material = materials.get(defaultIds::materials::opaque);
				monkey.localTransformMatrix = glm::mat4{ 1.0f };

				renderObjects.push_back(std::move(monkey));*/

			/*::RenderObject triangle{};
			triangle.mesh = *meshes.get(defaultIds::meshes::triangle);
			triangle.material = materials.get(defaultIds::materials::opaque);
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(5, 0, 5));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
			triangle.localTransformMatrix = translation * scale;
			renderObjects.push_back(std::move(triangle));*/
		}
	}

	void ReaShaderRenderer::_cleanupVulkan()
	{
		if (vktDevice)
			vkDeviceWaitIdle(vktDevice->vk()); // result ignored: a lost device must still be torn down
		// flush deletion queues in reverse order
		deletionQueues.vktFrameResized.flush();
		deletionQueues.vktPhysicalDeviceChanged.flush();
		deletionQueues.vktMain.flush();
		vktDevice = nullptr;
		vktPhysicalDevice = nullptr;
	}

} // namespace ReaShader
