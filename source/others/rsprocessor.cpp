#include "mypluginprocessor.h"

/* lice */
#pragma warning(push)
#pragma warning(disable: 26451)

#include "lice/lice.h"

#pragma warning(pop)
/* ---- */

#define PROJ_W 1280
#define PROJ_H 720
#define MAX_OBJECTS 100

namespace ReaShader {

	struct ids {

		enum descriptorBindings {
			gUb = 0,
			gUbD,
			oSb = 0,
			tCis = 0
		};

		enum commandBuffers {
			draw,
			transfer,
		};

		enum meshes {
			triangle,
			suzanne
		};

		enum textures {
			logo
		};

		enum materials {
			opaque
		};

	} ids;

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

	void drawFrame(ReaShaderProcessor* rs, double pushConstants[]) {

		vkt::vktDevice* vktDevice = rs->vktDevice;
		VkDevice device = rs->vktDevice->device;
		vkt::vktCommandPool* commandPool = rs->vktDevice->getGraphicsCommandPool();
		VkCommandBuffer commandBuffer = rs->vkDrawCommandBuffer;

		// begin command buffer
		commandPool->restartCommandBuffer(commandBuffer);

		// begin render pass
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = rs->vkRenderPass;
		renderPassInfo.framebuffer = rs->vkFramebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent.width = PROJ_W;
		renderPassInfo.renderArea.extent.height = PROJ_H;

		VkClearValue clearColor = { {{ 0.0f, 0.0f, 0.0f, 0.0f }} }; // transparent

		//clear depth at 1
		VkClearValue depthClear{};
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
		GPUCameraData camData{};
		camData.proj = projection;
		camData.view = view;
		camData.viewproj = projection * view;

		GPUSceneData sceneData{};
		sceneData.ambientColor = { sin(pushConstants[0]),0,cos(pushConstants[0]),1 };


		//and copy it to the buffer
		int* data{ nullptr };
		vmaMapMemory(vktDevice->vmaAllocator, rs->frameData.cameraBuffer->getAllocation(), (void**)&data);

		memcpy(data, &camData, sizeof(GPUCameraData));

		vmaUnmapMemory(vktDevice->vmaAllocator, rs->frameData.cameraBuffer->getAllocation());


		vmaMapMemory(vktDevice->vmaAllocator, rs->frameData.sceneBuffer->getAllocation(), (void**)&data);

		data += 0; // dynamic buffer offset

		memcpy(data, &sceneData, sizeof(GPUSceneData));

		vmaUnmapMemory(vktDevice->vmaAllocator, rs->frameData.sceneBuffer->getAllocation());


		std::vector<uint32_t> dynamicOffsets = { 0 }; // offset for each binding to a dynamic descriptor, in order of binding registration


		glm::mat4 model = glm::rotate(glm::mat4{ 1.0f }, (float)glm::radians(frameNumber * (float)pushConstants[2] * 5.f), glm::vec3(0, 1, 0));

		vmaMapMemory(vktDevice->vmaAllocator, rs->frameData.objectBuffer->getAllocation(), (void**)&data);

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
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &rs->frameData.globalSet.set, dynamicOffsets.size(), dynamicOffsets.data());

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &rs->frameData.objectSet.set, 0, nullptr);

				if (object.material->textureSet != VK_NULL_HANDLE) {
					VkDescriptorSet textureSet = object.material->textureSet->set;
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 2, 1, &textureSet, 0, nullptr);
				}
			}

			// set push constants

			//model rotation
			MeshPushConstants constants{};
			constants.render_matrix = model * object.transformMatrix;

			//upload the mesh to the GPU via push constants
#pragma warning(suppress: w_ptr_m_null) // assert material is not nullptr
			vkCmdPushConstants(commandBuffer, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

			//only bind the mesh if it's a different one from last bind
			if (object.mesh != lastMesh) {
				//bind the mesh vertex buffer with offset 0
				VkDeviceSize offset = 0;
				VkBuffer vertexBuffer = object.mesh->getVertexBuffer()->getBuffer();
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vertexBuffer), &offset);

				//only bind index buffer if it's used
				if (object.mesh->getIndexBuffer()->getBuffer())
					vkCmdBindIndexBuffer(commandBuffer, object.mesh->getIndexBuffer()->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

				lastMesh = object.mesh;
			}

			//we can now draw

			if (object.mesh->getIndexBuffer()->getBuffer())
				vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(object.mesh->getIndices().size()), 1, 0, 0, 0);
			else
				vkCmdDraw(commandBuffer, object.mesh->getVertices().size(), 1, 0, 0);
		}

		// unmap storage buffers
		vmaUnmapMemory(vktDevice->vmaAllocator, rs->frameData.objectBuffer->getAllocation());

		// end render pass

		vkCmdEndRenderPass(commandBuffer);

		// submit ( end command buffer )

		commandPool->submit(commandBuffer, VK_NULL_HANDLE, rs->vkRenderFinishedSemaphore, rs->vkImageAvailableSemaphore);
	}

	void transferFrame(ReaShaderProcessor* rs, int*& destBuffer) {

		// init command buffer

		vkt::vktDevice* vktDevice = rs->vktDevice;
		VkCommandBuffer commandBuffer = rs->vkTransferCommandBuffer;
		vkt::vktCommandPool* commandPool = vktDevice->getGraphicsCommandPool();

		vkQueueWaitIdle(vktDevice->getGraphicsQueue()->get());

		commandPool->restartCommandBuffer(commandBuffer);

		// Transition destination image to transfer destination layout

		vkt::commands::insertImageMemoryBarrier(
			commandBuffer,
			rs->vktFrameTransfer->getImage(),
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
		if (vktDevice->vktPhysicalDevice->supportsBlit())
		{
			// Define the region to blit (we will blit the whole swapchain image)
			VkOffset3D blitSize{};
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
				rs->vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				rs->vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
				rs->vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				rs->vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory later on
		vkt::commands::insertImageMemoryBarrier(
			commandBuffer,
			rs->vktFrameTransfer->getImage(),
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit queue (wait for draw frame)

		commandPool->submit(commandBuffer, rs->vkInFlightFence, VK_NULL_HANDLE, rs->vkRenderFinishedSemaphore);

		// wait fence since now we are on cpu

		vkWaitForFences(vktDevice->device, 1, &rs->vkInFlightFence, VK_TRUE, UINT64_MAX);
		vkResetFences(vktDevice->device, 1, &rs->vkInFlightFence);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;

		vkGetImageSubresourceLayout(vktDevice->device, rs->vktFrameTransfer->getImage(), &subResource, &subResourceLayout);

		// dest image is already mapped
		memcpy((void*)destBuffer, (void*)((int)(rs->vktFrameTransfer->getAllocationInfo()).pMappedData + subResourceLayout.offset), sizeof(LICE_pixel) * PROJ_W * PROJ_H);

	}

	// load vf bits to color attachment
	void loadBitsToImage(ReaShaderProcessor* rs, int* srcBuffer) {

		vkt::vktDevice* vktDevice = rs->vktDevice;
		vkt::vktCommandPool* commandPool = vktDevice->getGraphicsCommandPool();
		vkt::vktQueue* queue = vktDevice->getGraphicsQueue();

		vkQueueWaitIdle(queue->get());

		vktDevice->getGraphicsCommandPool()->restartCommandBuffer(rs->vkTransferCommandBuffer);

		vkt::commands::insertImageMemoryBarrier(
			rs->vkTransferCommandBuffer,
			rs->vktFrameTransfer->getImage(),
			0,
			VK_ACCESS_MEMORY_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit command, signal fence
		commandPool->submit(rs->vkTransferCommandBuffer, rs->vkInFlightFence, VK_NULL_HANDLE, VK_NULL_HANDLE);

		// wait for it
		vkWaitForFences(vktDevice->device, 1, &rs->vkInFlightFence, VK_TRUE, UINT64_MAX);
		vkResetFences(vktDevice->device, 1, &rs->vkInFlightFence);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;
		vkGetImageSubresourceLayout(vktDevice->device, rs->vktFrameTransfer->getImage(), &subResource, &subResourceLayout);

		// copy bits from src to framedest
		memcpy((void*)((int)(rs->vktFrameTransfer->getAllocationInfo()).pMappedData + subResourceLayout.offset), (void*)srcBuffer, sizeof(LICE_pixel) * PROJ_W * PROJ_H);

		commandPool->restartCommandBuffer(rs->vkTransferCommandBuffer);

		// retransition frametransfer to src copy optimal

		vkt::commands::insertImageMemoryBarrier(
			rs->vkTransferCommandBuffer,
			rs->vktFrameTransfer->getImage(),
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// color attachment goes dst optimal

		vkt::commands::insertImageMemoryBarrier(
			rs->vkTransferCommandBuffer,
			rs->vktColorAttachment->getImage(),
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
			rs->vktFrameTransfer->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			rs->vktColorAttachment->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&imageCopyRegion);

		commandPool->submit(rs->vkTransferCommandBuffer, VK_NULL_HANDLE, rs->vkImageAvailableSemaphore, VK_NULL_HANDLE);
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

			drawFrame(rsProcessor, pushConstants);

			transferFrame(rsProcessor, bits);

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

		vkt::physicalDevices::QueueFamilyIndices indices = vkt::physicalDevices::findQueueFamilies(device);

		// final condition

		return
			deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
			&& indices.graphicsFamily.has_value();
		;
	}

	// RENDER PASS

	VkRenderPass createRenderPass(vkt::vktDevice* vktDevice) {

		vkt::pipeline::renderPassBuilder renderPassBuilder(vktDevice);

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = VK_FORMAT_B8G8R8A8_UNORM;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // load existing attachment information
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // transition from dst to src optimal layout after render finished

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

		enum attachmentTags {
			colorAttTag,
			depthAttTag
		};

		enum subpassTags {
			mainSubpass
		};

		renderPassBuilder
			.addAttachment(std::move(colorAttachment), colorAttTag)
			.addAttachment(std::move(depth_attachment), depthAttTag);

		renderPassBuilder
			.initSubpass(VK_PIPELINE_BIND_POINT_GRAPHICS, mainSubpass)
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
		depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.srcAccessMask = 0;
		depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkSubpassDependency colorDependencyFinal{};
		colorDependencyFinal.srcSubpass = mainSubpass;
		colorDependencyFinal.dstSubpass = VK_SUBPASS_EXTERNAL;
		colorDependencyFinal.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		colorDependencyFinal.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		colorDependencyFinal.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		colorDependencyFinal.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		colorDependencyFinal.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		renderPassBuilder
			.addSubpassDependency(std::move(colorDependencyInit))
			.addSubpassDependency(std::move(depthDependency))
			.addSubpassDependency(std::move(colorDependencyFinal));

		VkRenderPass renderPass = renderPassBuilder.build();

		return renderPass;
	}

	// GRAPHICS PIPELINE

	vkt::Material createMaterialOpaque(vkt::vktDevice* vktDevice, VkRenderPass& renderPass, std::vector<VkDescriptorSetLayout> descriptorSetLayouts) {

		VkShaderModule vertShaderModule = vkt::pipeline::createShaderModule(vktDevice, "D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\shaders\\vert.spv");
		VkShaderModule fragShaderModule = vkt::pipeline::createShaderModule(vktDevice, "D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\shaders\\frag.spv");

		// ---------

		vkt::Material material{};

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

		VkPushConstantRange push_constant{};
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

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vktDevice->device, material.pipelineCache, 1, &pipelineInfo, nullptr, &(material.pipeline)));

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

	// MESH

	void loadTriangle(vkt::Mesh* mesh) {
		//make the array 3 vertices long
		std::vector<vkt::Vertex> vertices(3);

		//vertex positions
		vertices[0].position = { 1.f, 1.f, 0.0f };
		vertices[1].position = { -1.f, 1.f, 0.0f };
		vertices[2].position = { 0.f,-1.f, 0.0f };


		vertices[0].color = { 0.f, 1.f, 0.0f };
		vertices[1].color = { .5f, 1.f, 0.0f };
		vertices[2].color = { 1.f, 0.f, 0.5f };

		//we don't care about the vertex normals

		mesh->setVertices(std::move(vertices));
	}

	void ReaShaderProcessor::initVulkan() {

		// instance , device
		{
			myVkInstance = vkt::createVkInstance(vktDeletionQueue, "ReaShader Effect", "No Engine");

			std::vector<VkPhysicalDevice> vkSuitablePhysicalDevices = vkt::physicalDevices().enumerate(myVkInstance).removeUnsuitable(&isPhysicalDeviceSuitable).get();

			vktPhysicalDevice = new vkt::vktPhysicalDevice(vktDeletionQueue, myVkInstance, vkSuitablePhysicalDevices[0]); // choose the first suitable

			vktDevice = new vkt::vktDevice(vktDeletionQueue, vktPhysicalDevice);
		}

		// meshes

		{
			vkt::Mesh* triangleMesh = new vkt::Mesh(vktDevice);
			loadTriangle(triangleMesh);
			meshes.add(ids::meshes::triangle, triangleMesh);

			vkt::Mesh* suzanne = new vkt::Mesh(vktDevice);
			suzanne->load_from_obj("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\resource\\meshes\\Suzanne.obj");
			meshes.add(ids::meshes::suzanne, suzanne);
		}

		// textures

		{
			vkt::vktAllocatedImage* texture = new vkt::vktAllocatedImage(vktDevice);
			texture->createImage("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\resource\\images\\reashader text.png", VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			texture->createImageView(VK_IMAGE_VIEW_TYPE_2D, texture->getFormat(), VK_IMAGE_ASPECT_COLOR_BIT);
			textures.add(ids::textures::logo, texture);

			vkSampler = vkt::textures::createSampler(vktDevice, VK_FILTER_LINEAR);
		}

		// buffers

		vktDescriptorPool = new vkt::vktDescriptorPool(vktDevice,
			{
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 5 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 }
			}, 5);

		// set 0
		frameData.globalSet = vkt::descriptorSetLayoutBuilder(vktDevice)
			.bind(ids::descriptorBindings::gUb, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.bind(ids::descriptorBindings::gUbD, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();
		// set 1
		frameData.objectSet = vkt::descriptorSetLayoutBuilder(vktDevice)
			.bind(ids::descriptorBindings::oSb, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build();
		// set 3
		frameData.textureSet = vkt::descriptorSetLayoutBuilder(vktDevice)
			.bind(ids::descriptorBindings::tCis, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();

		vktDescriptorPool->allocateDescriptorSets({ frameData.globalSet, frameData.objectSet, frameData.textureSet });

		frameData.cameraBuffer = new vkt::vktAllocatedBuffer(vktDevice);
		frameData.cameraBuffer->allocate(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		frameData.sceneBuffer = new vkt::vktAllocatedBuffer(vktDevice);
		frameData.sceneBuffer->allocate(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		frameData.objectBuffer = new vkt::vktAllocatedBuffer(vktDevice);
		frameData.objectBuffer->allocate(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		vkt::descriptorSetWriter(vktDevice)
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
			.writeRegistered();

		// swap chain here (optional)

		// render target

		vktColorAttachment = new vkt::vktAllocatedImage(vktDevice);
		vktColorAttachment->createImage(
			{ PROJ_W, PROJ_H },
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		vktColorAttachment->createImageView(
			VK_IMAGE_VIEW_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_ASPECT_COLOR_BIT
		);

		// depth buffer

		vktDepthAttachment = new vkt::vktAllocatedImage(vktDevice);
		vktDepthAttachment->createImage(
			{ PROJ_W, PROJ_H },
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_D32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		vktDepthAttachment->createImageView(
			VK_IMAGE_VIEW_TYPE_2D,
			VK_FORMAT_D32_SFLOAT,
			VK_IMAGE_ASPECT_DEPTH_BIT
		);

		// frame transfer

		vktFrameTransfer = new vkt::vktAllocatedImage(vktDevice);
		vktFrameTransfer->createImage(
			{ PROJ_W, PROJ_H },
			VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // both src and dst for copy cmds
			VMA_MEMORY_USAGE_GPU_TO_CPU,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
		);

		// pipeline

		vkRenderPass = createRenderPass(vktDevice);
		vkFramebuffer = vkt::pipeline::createFramebuffer(vktDevice, vkRenderPass, { PROJ_W,PROJ_H }, { vktColorAttachment, vktDepthAttachment });

		// Materials

		{
			vkt::Material material_opaque = createMaterialOpaque(vktDevice, vkRenderPass, { frameData.globalSet.layout , frameData.objectSet.layout , frameData.textureSet.layout });
			material_opaque.textureSet = &frameData.textureSet;
			materials.add(ids::materials::opaque, std::move(material_opaque));
		}

		// render objects

		{
			vkt::RenderObject monkey{};
			monkey.mesh = *meshes.get(ids::meshes::suzanne);
			monkey.material = materials.get(ids::materials::opaque);
			monkey.transformMatrix = glm::mat4{ 1.0f };

			renderObjects.push_back(std::move(monkey));

			vkt::RenderObject triangle{};
			triangle.mesh = *meshes.get(ids::meshes::triangle);
			triangle.material = materials.get(ids::materials::opaque);
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(5, 0, 5));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
			triangle.transformMatrix = translation * scale;
			renderObjects.push_back(std::move(triangle));
		}

		// command buffers
		{
			vktDevice->getGraphicsCommandPool()
				->createCommandBuffer(ids::commandBuffers::draw)
				->createCommandBuffer(ids::commandBuffers::transfer);

			vkDrawCommandBuffer = vktDevice->getGraphicsCommandPool()->get(ids::commandBuffers::draw);
			vkTransferCommandBuffer = vktDevice->getGraphicsCommandPool()->get(ids::commandBuffers::transfer);
		}

		vkInFlightFence = vkt::syncObjects::createFence(vktDevice, false);
		vkRenderFinishedSemaphore = vkt::syncObjects::createSemaphore(vktDevice);
		vkImageAvailableSemaphore = vkt::syncObjects::createSemaphore(vktDevice);

		// check init properties

		// Check blit support for source and destination
		if (!vktPhysicalDevice->supportsBlit())
		{
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
		}
	}

	void ReaShaderProcessor::cleanupVulkan() {
		vktDevice->waitIdle();
		vktDeletionQueue.flush();
	}
}

