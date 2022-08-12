#include "mypluginprocessor.h"

#include "vktools/vktools.h"

#define PROJ_W 1280
#define PROJ_H 720
#define MAX_OBJECTS 100

namespace ReaShader {

	// DRAW

	struct MeshPushConstants {
		glm::vec4 data;
		glm::mat4 render_matrix;
	};

	struct GPUCameraData {
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 viewproj;
	};

	struct GPUSceneData {
		glm::vec4 fogColor; // w is for exponent
		glm::vec4 fogDistances; //x for min, y for max, zw unused.
		glm::vec4 ambientColor;
		glm::vec4 sunlightDirection; //w for sun power
		glm::vec4 sunlightColor;
	};

	struct GPUObjectData {
		glm::mat4 modelMatrix;
	};

	void drawFrame(ReaShaderProcessor* rs, vkt::vktDevice* vktDevice,
		VkRenderPass renderPass,
		VkFramebuffer frameBuffer,
		VkCommandBuffer& commandBuffer,
		VkSemaphore& renderFinishedSemaphore,
		VkFence& inFlightFence,
		double pushConstants[]
	) {

		VkDevice device = vktDevice->device;

		// begin command buffer
		vktDevice->restartCommandBuffer(commandBuffer);

		// begin render pass
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = frameBuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent.width = PROJ_W;
		renderPassInfo.renderArea.extent.height = PROJ_H;

		VkClearValue clearColor = { {{ 0.0f, 0.0f, 0.0f, 0.0f }} }; // transparent

		//clear depth at 1
		VkClearValue depthClear;
		depthClear.depthStencil.depth = 1.f;

		std::vector<VkClearValue> clearValues = { clearColor, depthClear };

		renderPassInfo.clearValueCount = clearValues.size();
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// render objects

		double frameNumber = pushConstants[0] * pushConstants[1]; // proj_time * frate

		//camera position
		glm::vec3 camPos = { 0.f,-6.f,-10.f };
		glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
		//camera projection
		glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.0f);
		projection[1][1] *= -1;

		//fill a GPU camera data struct
		GPUCameraData camData;
		camData.proj = projection;
		camData.view = view;
		camData.viewproj = projection * view;

		GPUSceneData sceneData;
		sceneData.ambientColor = { sin(pushConstants[0]),0,cos(pushConstants[0]),1 };


		//and copy it to the buffer
		int* data;
		vmaMapMemory(vktDevice->vmaAllocator, rs->frameData.cameraBuffer.allocation, (void**)&data);

		memcpy(data, &camData, sizeof(GPUCameraData));

		vmaUnmapMemory(vktDevice->vmaAllocator, rs->frameData.cameraBuffer.allocation);


		vmaMapMemory(vktDevice->vmaAllocator, rs->frameData.sceneBuffer.allocation, (void**)&data);

		data += 0; // dynamic buffer offset

		memcpy(data, &sceneData, sizeof(GPUSceneData));

		vmaUnmapMemory(vktDevice->vmaAllocator, rs->frameData.sceneBuffer.allocation);


		std::vector<uint32_t> dynamicOffsets = { 0 }; // offset for each binding to a dynamic descriptor, in order of binding registration


		glm::mat4 model = glm::rotate(glm::mat4{ 1.0f }, (float)glm::radians(frameNumber * (float)pushConstants[2] * 5.f), glm::vec3(0, 1, 0));

		vmaMapMemory(vktDevice->vmaAllocator, rs->frameData.objectBuffer.allocation, (void**)&data);

		GPUObjectData* objectSSBO = (GPUObjectData*)data;

		vkt::Mesh* lastMesh = nullptr;
		vkt::Material* lastMaterial = nullptr;
		for (int i = 0; i < rs->renderObjects.size(); i++)
		{
			vkt::RenderObject& object = rs->renderObjects[i];

			// write storage buffers
			objectSSBO[i].modelMatrix = object.transformMatrix * model;

			//only bind the pipeline if it doesn't match with the already bound one
			if (object.material != lastMaterial) {
				// dynamic states

				VkViewport viewport{};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = static_cast<float>(PROJ_W);
				viewport.height = static_cast<float>(PROJ_H);
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor{};
				scissor.offset = { 0, 0 };
				scissor.extent.width = PROJ_W;
				scissor.extent.height = PROJ_H;
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
				lastMaterial = object.material;

				//bind the descriptor set when changing pipeline
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &rs->frameData.globalDescriptor, dynamicOffsets.size(), dynamicOffsets.data());

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &rs->frameData.objectDescriptor, 0, nullptr);
			}

			// set push constants

			//model rotation

			MeshPushConstants constants;
			constants.render_matrix = model * object.transformMatrix;

			//upload the mesh to the GPU via push constants
			vkCmdPushConstants(commandBuffer, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

			//only bind the mesh if it's a different one from last bind
			if (object.mesh != lastMesh) {
				//bind the mesh vertex buffer with offset 0
				VkDeviceSize offset = 0;
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &object.mesh->vertexBuffer.buffer, &offset);
				lastMesh = object.mesh;
			}
			//we can now draw
			vkCmdDraw(commandBuffer, object.mesh->vertices.size(), 1, 0, 0);
		}

		// unmap storage buffers
		vmaUnmapMemory(vktDevice->vmaAllocator, rs->frameData.objectBuffer.allocation);

		// end render pass

		vkCmdEndRenderPass(commandBuffer);

		// submit ( end command buffer )

		vktDevice->submitQueue(commandBuffer, VK_NULL_HANDLE, renderFinishedSemaphore, rs->vkImageAvailableSemaphore);
	}

	void transferFrame(vkt::vktDevice* vktDevice,
		vkt::vktInitProperties vktInitProperties,
		VkImage srcImage,
		vkt::AllocatedImage* vkFrameTransfer,
		int*& destBuffer,
		VkCommandBuffer& commandBuffer,
		VkSemaphore& renderFinishedSemaphore,
		VkFence& inFlightFence
	) {

		// init command buffer

		vkQueueWaitIdle(vktDevice->transferQueue);

		vktDevice->restartCommandBuffer(commandBuffer);

		// Transition destination image to transfer destination layout

		vkt::insertImageMemoryBarrier(
			commandBuffer,
			vkFrameTransfer->image,
			0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// srcImage is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need to be transitioned

		// do the blit/copy

		// If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from BGR to RGB)
		if (vktInitProperties.supportsBlit)
		{
			// Define the region to blit (we will blit the whole swapchain image)
			VkOffset3D blitSize;
			blitSize.x = PROJ_W;
			blitSize.y = PROJ_H;
			blitSize.z = 1;
			VkImageBlit imageBlitRegion{};
			imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.srcSubresource.layerCount = 1;
			imageBlitRegion.srcOffsets[1] = blitSize;
			imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.dstSubresource.layerCount = 1;
			imageBlitRegion.dstOffsets[1] = blitSize;

			// Issue the blit command
			vkCmdBlitImage(
				commandBuffer,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vkFrameTransfer->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageBlitRegion,
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
			imageCopyRegion.extent.width = PROJ_W;
			imageCopyRegion.extent.height = PROJ_H;
			imageCopyRegion.extent.depth = 1;

			// Issue the copy command
			vkCmdCopyImage(
				commandBuffer,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vkFrameTransfer->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory later on
		vkt::insertImageMemoryBarrier(
			commandBuffer,
			vkFrameTransfer->image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit queue (wait for draw frame)

		vktDevice->submitQueue(commandBuffer, inFlightFence, VK_NULL_HANDLE, renderFinishedSemaphore, vktDevice->transferQueue);

		// wait fence since now we are on cpu

		vkWaitForFences(vktDevice->device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
		vkResetFences(vktDevice->device, 1, &inFlightFence);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;

		vkGetImageSubresourceLayout(vktDevice->device, vkFrameTransfer->image, &subResource, &subResourceLayout);

		// dest image is already mapped
		memcpy((void*)destBuffer, (void*)((int)(vkFrameTransfer->allocationInfo).pMappedData + subResourceLayout.offset), sizeof(LICE_pixel) * PROJ_W * PROJ_H);

	}

	// load vf bits to color attachment
	void loadBitsToImage(ReaShaderProcessor* rs, int* srcBuffer) {

		vkQueueWaitIdle(rs->vktDevice->transferQueue);

		rs->vktDevice->restartCommandBuffer(rs->vkTransferCommandBuffer);

		vkt::insertImageMemoryBarrier(
			rs->vkTransferCommandBuffer,
			rs->vkFrameTransfer->image,
			0,
			VK_ACCESS_MEMORY_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit command, signal fence
		rs->vktDevice->submitQueue(rs->vkTransferCommandBuffer, rs->vkInFlightFence, VK_NULL_HANDLE, VK_NULL_HANDLE, rs->vktDevice->transferQueue);

		// wait for it
		vkWaitForFences(rs->vktDevice->device, 1, &rs->vkInFlightFence, VK_TRUE, UINT64_MAX);
		vkResetFences(rs->vktDevice->device, 1, &rs->vkInFlightFence);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;
		vkGetImageSubresourceLayout(rs->vktDevice->device, rs->vkFrameTransfer->image, &subResource, &subResourceLayout);

		// copy bits from src to framedest
		memcpy((void*)((int)(rs->vkFrameTransfer->allocationInfo).pMappedData + subResourceLayout.offset), (void*)srcBuffer, sizeof(LICE_pixel) * PROJ_W * PROJ_H);

		rs->vktDevice->restartCommandBuffer(rs->vkTransferCommandBuffer);

		// retransition frametransfer to src copy optiman

		vkt::insertImageMemoryBarrier(
			rs->vkTransferCommandBuffer,
			rs->vkFrameTransfer->image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// color attachment goes dst optimal

		vkt::insertImageMemoryBarrier(
			rs->vkTransferCommandBuffer,
			rs->vkColorAttachment->image,
			0,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// perform copy to color attachment

		VkImageCopy imageCopyRegion{};
		imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.srcSubresource.layerCount = 1;
		imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.dstSubresource.layerCount = 1;
		imageCopyRegion.extent.width = PROJ_W;
		imageCopyRegion.extent.height = PROJ_H;
		imageCopyRegion.extent.depth = 1;

		vkCmdCopyImage(
			rs->vkTransferCommandBuffer,
			rs->vkFrameTransfer->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			rs->vkColorAttachment->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&imageCopyRegion);

		rs->vktDevice->submitQueue(rs->vkTransferCommandBuffer, VK_NULL_HANDLE, rs->vkImageAvailableSemaphore, VK_NULL_HANDLE, rs->vktDevice->transferQueue);
	}

	/* video */

	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut)
	{
		// called from video thread, gets current state

		if (idx >= 0 && idx < ReaShader::uNumParams)
		{
			RSData* rsData = (RSData*)vproc->userdata;
			// directly pass parameters to the video processor
			//*valueOut = (*((std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>*) (((int**)vproc->userdata)[0]))).at(idx);
			*valueOut = (*(rParamsMap*)rsData->get(0)).at(idx);

			return true;
		}
		return false;
	}

	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms, double project_time, double frate, int force_format)
	{
		IVideoFrame* vf = vproc->renderInputVideoFrame(0, 'RGBA');

		if (vf)
		{
			int* bits = vf->get_bits();
			//int rowspan = vf->get_rowspan() / 4;
			int w = vf->get_w();
			int h = vf->get_h();
			int fmt = vf->get_fmt();

			LICE_WrapperBitmap* bitmap = new LICE_WrapperBitmap((LICE_pixel*)bits, w, h, w, false);

			RSData* rsData = (RSData*)vproc->userdata;
			ReaShaderProcessor* rsProcessor = (ReaShaderProcessor*)rsData->get(1);

			/*for (int y = 0; y < parmlist[uVideoParam + 1] * h; y++) {
				for (int x = 0; x < w; x++) {
					LICE_PutPixel(bitmap, x, y, rsProcessor->myColor, 0xff, LICE_BLIT_MODE_COPY);
				}
			}*/

			loadBitsToImage(rsProcessor, bits);

			float rotSpeed = parmlist[uVideoParam + 1];
			double pushConstants[] = { project_time, frate, rotSpeed };

			drawFrame(rsProcessor, rsProcessor->vktDevice,
				rsProcessor->vkRenderPass,
				rsProcessor->vkFramebuffer,
				rsProcessor->vkDrawCommandBuffer,
				rsProcessor->vkRenderFinishedSemaphore,
				rsProcessor->vkInFlightFence,
				pushConstants
			);

			transferFrame(rsProcessor->vktDevice,
				rsProcessor->vktInitProperties,
				rsProcessor->vkColorAttachment->image,
				rsProcessor->vkFrameTransfer,
				bits,
				rsProcessor->vkTransferCommandBuffer,
				rsProcessor->vkRenderFinishedSemaphore,
				rsProcessor->vkInFlightFence);

			//  parmlist[0] is wet, [1] is parameter
		}
		return vf;
	}

	/* vulkan */

	bool isPhysicalDeviceSuitable(VkPhysicalDevice device) {

		// physical device properties

		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		// queue families

		vkt::QueueFamilyIndices indices = vkt::findQueueFamilies(device);

		// final condition

		return
			deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
			&& indices.graphicsFamily.has_value();
		;
	}

	// RENDER PASS

	VkRenderPass createRenderPass(VkDevice device) {

		// color

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = VK_FORMAT_B8G8R8A8_UNORM;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // load existing attachment information
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // transition from dst to src optimal layout after render finished

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// depth

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

		VkAttachmentReference depth_attachment_ref = {};
		depth_attachment_ref.attachment = 1;
		depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// subpass

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depth_attachment_ref;

		// Use subpass dependencies for layout transitions
		VkSubpassDependency colorDependencyInit{};
		colorDependencyInit.srcSubpass = VK_SUBPASS_EXTERNAL;
		colorDependencyInit.dstSubpass = 0;
		colorDependencyInit.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		colorDependencyInit.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		colorDependencyInit.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		colorDependencyInit.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		colorDependencyInit.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkSubpassDependency depthDependency{};
		depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		depthDependency.dstSubpass = 0;
		depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.srcAccessMask = 0;
		depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkSubpassDependency colorDependencyFinal{};
		colorDependencyFinal.srcSubpass = 0;
		colorDependencyFinal.dstSubpass = VK_SUBPASS_EXTERNAL;
		colorDependencyFinal.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		colorDependencyFinal.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		colorDependencyFinal.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		colorDependencyFinal.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		colorDependencyFinal.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		std::vector<VkSubpassDependency> dependencies = { colorDependencyInit, depthDependency, colorDependencyFinal };

		std::vector<VkAttachmentDescription> attachments = { colorAttachment, depth_attachment };

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = attachments.size();
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VkRenderPass renderPass;

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

		return renderPass;
	}

	// GRAPHICS PIPELINE

	vkt::Material createMaterialOpaque(vkt::vktDevice* vktDevice, VkRenderPass& renderPass, std::vector<VkDescriptorSetLayout> descriptorSetLayouts) {

		VkShaderModule vertShaderModule = vktDevice->createShaderModule("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\shaders\\vert.spv");
		VkShaderModule fragShaderModule = vktDevice->createShaderModule("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\shaders\\frag.spv");

		// ---------

		vkt::Material material;

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
		vertexInputInfo.vertexBindingDescriptionCount = vertexInputDesc.bindings.size();
		vertexInputInfo.pVertexBindingDescriptions = vertexInputDesc.bindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = vertexInputDesc.attributes.size();
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
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		// multisample
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		// color blend attachment
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		/*	Color blend Pseudocode:

			if (blendEnable) {
				finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
				finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
			} else {
				finalColor = newColor;
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

		VkPushConstantRange push_constant;
		push_constant.offset = 0;
		push_constant.size = sizeof(MeshPushConstants);
		push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		// pipeline layout

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = descriptorSetLayouts.size();
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &push_constant;

		VK_CHECK_RESULT(vkCreatePipelineLayout(vktDevice->device, &pipelineLayoutInfo, nullptr, &(material.pipelineLayout)));

		// pipeline cache

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(vktDevice->device, &pipelineCacheCreateInfo, nullptr, &(material.pipelineCache)));

		// pipeline

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		// shader stages
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());;
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
		pipelineInfo.basePipelineIndex = -1; // Optional
		// renderpass
		pipelineInfo.renderPass = renderPass;
		//pipelineInfo.subpass = 0;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vktDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &(material.pipeline)));

		vktDevice->pDeletionQueue->push_function([=]() {
			vkDestroyPipeline(vktDevice->device, material.pipeline, nullptr);
			vkDestroyPipelineCache(vktDevice->device, material.pipelineCache, nullptr);
			vkDestroyPipelineLayout(vktDevice->device, material.pipelineLayout, nullptr);
			});

		// ---------

		vkDestroyShaderModule(vktDevice->device, fragShaderModule, nullptr);
		vkDestroyShaderModule(vktDevice->device, vertShaderModule, nullptr);

		return material;
	}

	// FRAMEBUFFER

	VkFramebuffer createFramebuffer(VkDevice device, VkImageView colorView, VkImageView depthView, VkRenderPass renderPass) {
		std::vector<VkImageView> attachments = { colorView, depthView };

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = attachments.size();
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = PROJ_W;
		framebufferInfo.height = PROJ_H;
		framebufferInfo.layers = 1;

		VkFramebuffer frameBuffer;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffer));

		return frameBuffer;
	}

	// MESH

	void loadTriangle(vkt::Mesh* mesh) {
		//make the array 3 vertices long
		mesh->vertices.resize(3);

		//vertex positions
		mesh->vertices[0].position = { 1.f, 1.f, 0.0f };
		mesh->vertices[1].position = { -1.f, 1.f, 0.0f };
		mesh->vertices[2].position = { 0.f,-1.f, 0.0f };

		//vertex colors, all green
		mesh->vertices[0].color = { 0.f, 1.f, 0.0f }; //pure green
		mesh->vertices[1].color = { .5f, 1.f, 0.0f };
		mesh->vertices[2].color = { 1.f, 0.f, 0.5f };

		//we don't care about the vertex normals

		mesh->verticesToBuffer();
	}

	void ReaShaderProcessor::initVulkan() {

		// instance , device

		myVkInstance = vkt::createVkInstance(deletionQueue, "ReaShader Effect", "No Engine");

		vktPhysicalDevice = new vkt::vktPhysicalDevice(deletionQueue, myVkInstance, vkt::pickSuitablePhysicalDevice(myVkInstance, &isPhysicalDeviceSuitable));

		vktDevice = new vkt::vktDevice(deletionQueue, vktPhysicalDevice);

		// meshes

		vkt::Mesh* triangleMesh = new vkt::Mesh(vktDevice);
		loadTriangle(triangleMesh);

		vkt::Mesh* suzanne = new vkt::Mesh(vktDevice);
		suzanne->load_from_obj("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\resource\\meshes\\Suzanne.obj");

		meshes.add("suzanne", suzanne);
		meshes.add("triangle", triangleMesh);

		// buffers

		vkDescriptorPool = vktDevice->createDescriptorPool({
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 5 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 }
			}, 2);

		// set 0
		vkGlobalSetLayout = vktDevice->createDescriptorSetLayout({
			vkt::createDescriptorSetLayoutBinding(0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT) ,
			vkt::createDescriptorSetLayoutBinding(1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
			});
		vktDevice->allocateDescriptorSet(vkDescriptorPool, &vkGlobalSetLayout, &(frameData.globalDescriptor));

		frameData.cameraBuffer.createBuffer(vktDevice, sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		frameData.sceneBuffer.createBuffer(vktDevice, vktPhysicalDevice->pad_uniform_buffer_size(sizeof(GPUSceneData)), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		vkt::writeDescriptorSet(vktDevice, frameData.globalDescriptor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameData.cameraBuffer, sizeof(GPUCameraData), 0, 0);
		vkt::writeDescriptorSet(vktDevice, frameData.globalDescriptor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frameData.sceneBuffer, sizeof(GPUSceneData), 0, 1);

		// set 1
		vkObjectSetLayout = vktDevice->createDescriptorSetLayout({
			vkt::createDescriptorSetLayoutBinding(0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_SHADER_STAGE_VERTEX_BIT) ,
			});
		vktDevice->allocateDescriptorSet(vkDescriptorPool, &vkObjectSetLayout, &(frameData.objectDescriptor));

		frameData.objectBuffer.createBuffer(vktDevice, sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		vkt::writeDescriptorSet(vktDevice, frameData.objectDescriptor, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frameData.objectBuffer, sizeof(GPUObjectData) * MAX_OBJECTS, 0, 0);

		// swap chain here (optional)

		// render target

		vkColorAttachment = new vkt::AllocatedImage(vktDevice);

		vkColorAttachment->createImage(
			{ PROJ_W, PROJ_H },
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		vkColorAttachment->createImageView(
			VK_IMAGE_VIEW_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_ASPECT_COLOR_BIT
		);

		// depth buffer

		vkDepthAttachment = new vkt::AllocatedImage(vktDevice);
		vkDepthAttachment->createImage(
			{ PROJ_W, PROJ_H },
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_D32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		vkDepthAttachment->createImageView(
			VK_IMAGE_VIEW_TYPE_2D,
			VK_FORMAT_D32_SFLOAT,
			VK_IMAGE_ASPECT_DEPTH_BIT
		);

		// frame transfer

		vkFrameTransfer = new vkt::AllocatedImage(vktDevice);
		vkFrameTransfer->createImage(
			{ PROJ_W, PROJ_H },
			VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // both src and dst for copy cmds
			VMA_MEMORY_USAGE_GPU_TO_CPU,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
		);

		// pipeline

		vkRenderPass = createRenderPass(vktDevice->device);
		deletionQueue.push_function([=]() { vkDestroyRenderPass(vktDevice->device, vkRenderPass, nullptr); });

		vkFramebuffer = createFramebuffer(vktDevice->device, vkColorAttachment->imageView, vkDepthAttachment->imageView, vkRenderPass);
		deletionQueue.push_function([=]() { vkDestroyFramebuffer(vktDevice->device, vkFramebuffer, nullptr); });

		vkt::Material material_opaque = createMaterialOpaque(vktDevice, vkRenderPass, { vkGlobalSetLayout , vkObjectSetLayout });
		materials.add("opaque", material_opaque);

		// render objects

		vkt::RenderObject monkey;
		monkey.mesh = *meshes.get("suzanne");
		monkey.material = materials.get("opaque");
		monkey.transformMatrix = glm::mat4{ 1.0f };

		renderObjects.push_back(monkey);

		vkt::RenderObject triangle;
		triangle.mesh = *meshes.get("triangle");
		triangle.material = materials.get("opaque");
		glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(5, 0, 5));
		glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
		triangle.transformMatrix = translation * scale;
		renderObjects.push_back(triangle);

		vkDrawCommandBuffer = vktDevice->createCommandBuffer();
		vkTransferCommandBuffer = vktDevice->createCommandBuffer(vktDevice->transferCommandPool);

		vkInFlightFence = vktDevice->createFence(false, deletionQueue);
		vkRenderFinishedSemaphore = vktDevice->createSemaphore(deletionQueue);
		vkImageAvailableSemaphore = vktDevice->createSemaphore(deletionQueue);

		// check init properties

		// Check blit support for source and destination
		if (!(vktDevice->vktPhysicalDevice->getFormatProperties(VK_FORMAT_R8G8B8A8_UNORM).linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
		{
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
		}
		else {
			vktInitProperties.supportsBlit = true;
		}
	}

	void ReaShaderProcessor::cleanupVulkan() {
		vktDevice->waitIdle();
		deletionQueue.flush();
	}
}

