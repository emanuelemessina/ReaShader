#include "mypluginprocessor.h"

#define PROJ_W 1280
#define PROJ_H 720

namespace ReaShader {

	// DRAW

	void drawFrame(vkt::vktDevice* vktDevice,
		VkRenderPass renderPass,
		vkt::Mesh mesh,
		VkPipeline graphicsPipeline,
		VkFramebuffer frameBuffer,
		VkCommandBuffer& drawCommandBuffer,
		VkSemaphore& renderFinishedSemaphore,
		VkFence& inFlightFence) {

		VkDevice device = vktDevice->device;

		// begin command buffer
		VkCommandBuffer commandBuffer = vktDevice->createCommandBuffer(nullptr, drawCommandBuffer);
		drawCommandBuffer = commandBuffer;

		// begin render pass
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = frameBuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent.width = PROJ_W;
		renderPassInfo.renderArea.extent.height = PROJ_H;

		VkClearValue clearColor = { {{ 0.0f, 0.0f, 0.2f, 1.0f }} };
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		//bind pipeline

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

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

		// bind vertex buffers

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);

		// draw

		vkCmdDraw(commandBuffer, mesh.vertices.size(), 1, 0, 0);

		// end render pass

		vkCmdEndRenderPass(commandBuffer);

		// submit ( end command buffer )

		vktDevice->submitQueue(commandBuffer, VK_NULL_HANDLE, renderFinishedSemaphore, VK_NULL_HANDLE);
	}

	void transferFrame(vkt::vktDevice* vktDevice,
		VkImage srcImage,
		int*& destBuffer,
		VkCommandBuffer& prevCommandBuffer,
		VkSemaphore& renderFinishedSemaphore,
		VkFence& inFlightFence);

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

			drawFrame(rsProcessor->vktDevice,
				rsProcessor->vkRenderPass,
				rsProcessor->myMesh,
				rsProcessor->vkGraphicsPipeline,
				rsProcessor->vkFramebuffer,
				rsProcessor->vkDrawCommandBuffer,
				rsProcessor->vkRenderFinishedSemaphore,
				rsProcessor->vkInFlightFence);

			transferFrame(rsProcessor->vktDevice,
				rsProcessor->vkColorAttachment->image,
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

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = VK_FORMAT_B8G8R8A8_UNORM;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 1> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VkRenderPass renderPass;

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

		return renderPass;
	}

	// GRAPHICS PIPELINE

	VkPipeline createGraphicsPipeline(vkt::vktDevice* vktDevice, VkRenderPass& renderPass, VkPipelineLayout* pipelineLayout, VkPipelineCache* pipelineCache) {

		VkShaderModule vertShaderModule = vktDevice->createShaderModule("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\shaders\\vert.spv");
		VkShaderModule fragShaderModule = vktDevice->createShaderModule("D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\shaders\\frag.spv");

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

		// rasterization
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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

		// pipeline layout

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 0; // Optional
		pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
		pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
		pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

		if (vkCreatePipelineLayout(vktDevice->device, &pipelineLayoutInfo, nullptr, pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout!");
		}

		// pipeline cache

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(vktDevice->device, &pipelineCacheCreateInfo, nullptr, pipelineCache));

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
		pipelineInfo.pDepthStencilState = nullptr; // Optional
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		// layout
		pipelineInfo.layout = *pipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional
		// renderpass
		pipelineInfo.renderPass = renderPass;
		//pipelineInfo.subpass = 0;

		VkPipeline graphicsPipeline;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(vktDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline));

		// ---------

		vkDestroyShaderModule(vktDevice->device, fragShaderModule, nullptr);
		vkDestroyShaderModule(vktDevice->device, vertShaderModule, nullptr);

		return graphicsPipeline;
	}

	// FRAMEBUFFER

	VkFramebuffer createFramebuffer(VkDevice device, VkImageView imageView, VkRenderPass renderPass) {
		VkImageView attachments[] = { imageView };

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = PROJ_W;
		framebufferInfo.height = PROJ_H;
		framebufferInfo.layers = 1;

		VkFramebuffer frameBuffer;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffer));

		return frameBuffer;
	}

	void transferFrame(vkt::vktDevice* vktDevice,
		VkImage srcImage,
		int*& destBuffer,
		VkCommandBuffer& transferCommandBuffer,
		VkSemaphore& renderFinishedSemaphore,
		VkFence& inFlightFence
	) {

		int* imagedata;

		// create dest image

		vkt::vktAttachment* dest = new vkt::vktAttachment(vktDevice);
		dest->createImage(
			PROJ_W, PROJ_H,
			VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);


		// init command buffer

		VkCommandBuffer commandBuffer = vktDevice->createCommandBuffer(nullptr, transferCommandBuffer);
		transferCommandBuffer = commandBuffer;

		// Transition destination image to transfer destination layout

		vkt::insertImageMemoryBarrier(
			commandBuffer,
			dest->image,
			0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// srcImage is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need to be transitioned

		// do the blit/copy

		bool supportsBlit = true;

		// Check blit support for source and destination
		VkFormatProperties formatProps;

		// Check if the device supports blitting to linear images
		vkGetPhysicalDeviceFormatProperties(vktDevice->vktPhysicalDevice->physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
		if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
			supportsBlit = false;
		}

		// If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from BGR to RGB)
		if (supportsBlit)
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
				dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
				dest->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory later on
		vkt::insertImageMemoryBarrier(
			commandBuffer,
			dest->image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// submit queue (wait for draw frame)

		vktDevice->submitQueue(commandBuffer, inFlightFence, VK_NULL_HANDLE, renderFinishedSemaphore);

		// wait fence since now we are on cpu

		vkWaitForFences(vktDevice->device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
		vkResetFences(vktDevice->device, 1, &inFlightFence);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout subResourceLayout;

		vkGetImageSubresourceLayout(vktDevice->device, dest->image, &subResource, &subResourceLayout);

		// Map image memory so we can start copying from it
		vkMapMemory(vktDevice->device, dest->memory, 0, VK_WHOLE_SIZE, 0, (void**)&imagedata);
		imagedata += subResourceLayout.offset;

		memcpy((void*)destBuffer, (void*)imagedata, sizeof(LICE_pixel) * PROJ_W * PROJ_H);

		// Clean up resources
		delete(dest);
	}

	// MESH

	void loadMesh(vkt::Mesh* mesh) {
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
	}
	void allocateMesh(VmaAllocator vmaAllocator, vkt::Mesh* mesh) {
		//allocate vertex buffer
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		//this is the total size, in bytes, of the buffer we are allocating
		bufferInfo.size = mesh->vertices.size() * sizeof(vkt::Vertex);
		//this buffer is going to be used as a Vertex Buffer
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VmaAllocationCreateInfo vmaallocInfo = {};
		vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

		//allocate the buffer
		VK_CHECK_RESULT(vmaCreateBuffer(vmaAllocator, &bufferInfo, &vmaallocInfo,
			&(mesh->vertexBuffer.buffer),
			&(mesh->vertexBuffer.allocation),
			nullptr));

		//copy vertex data
		void* data;
		vmaMapMemory(vmaAllocator, mesh->vertexBuffer.allocation, &data);

		memcpy(data, mesh->vertices.data(), mesh->vertices.size() * sizeof(vkt::Vertex));

		vmaUnmapMemory(vmaAllocator, mesh->vertexBuffer.allocation);
	}

	void ReaShaderProcessor::initVulkan() {

		// instance , device

		myVkInstance = vkt::createVkInstance("ReaShader Effect", "No Engine");
		deletionQueue.push_function([=]() { vkDestroyInstance(myVkInstance, nullptr); });

		vktPhysicalDevice = new vkt::vktPhysicalDevice(vkt::pickSuitablePhysicalDevice(myVkInstance, &isPhysicalDeviceSuitable));
		deletionQueue.push_function([=]() { delete(vktPhysicalDevice); });

		vktDevice = new vkt::vktDevice(vktPhysicalDevice);
		deletionQueue.push_function([=]() { delete(vktDevice); });

		// memory allocator

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = vktPhysicalDevice->physicalDevice;
		allocatorInfo.device = vktDevice->device;
		allocatorInfo.instance = myVkInstance;
		vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
		deletionQueue.push_function([=]() { vmaDestroyAllocator(vmaAllocator); });

		// meshes

		loadMesh(&myMesh);
		allocateMesh(vmaAllocator, &myMesh);
		deletionQueue.push_function([=]() { vmaDestroyBuffer(vmaAllocator, myMesh.vertexBuffer.buffer, myMesh.vertexBuffer.allocation); });

		// swap chain here (optional)

		// render target

		vkColorAttachment = new vkt::vktAttachment(vktDevice);
		deletionQueue.push_function([=]() { delete(vkColorAttachment); });

		vkColorAttachment->createImage(
			PROJ_W, PROJ_H,
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		vkColorAttachment->createImageView(
			VK_IMAGE_VIEW_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_ASPECT_COLOR_BIT
		);

		vkRenderPass = createRenderPass(vktDevice->device);
		deletionQueue.push_function([=]() { vkDestroyRenderPass(vktDevice->device, vkRenderPass, nullptr); });

		vkFramebuffer = createFramebuffer(vktDevice->device, vkColorAttachment->imageView, vkRenderPass);
		deletionQueue.push_function([=]() { vkDestroyFramebuffer(vktDevice->device, vkFramebuffer, nullptr); });

		vkGraphicsPipeline = createGraphicsPipeline(vktDevice, vkRenderPass, &vkPipelineLayout, &vkPipelineCache);
		deletionQueue.push_function([=]() {
			vkDestroyPipeline(vktDevice->device, vkGraphicsPipeline, nullptr);
			vkDestroyPipelineCache(vktDevice->device, vkPipelineCache, nullptr);
			vkDestroyPipelineLayout(vktDevice->device, this->vkPipelineLayout, nullptr);
			});

		vkDrawCommandBuffer = vktDevice->createCommandBuffer();
		vkTransferCommandBuffer = vktDevice->createCommandBuffer();
		deletionQueue.push_function([=]() {
			std::vector<VkCommandBuffer> cmds = { vkDrawCommandBuffer , vkTransferCommandBuffer };
			vkFreeCommandBuffers(vktDevice->device, vktDevice->commandPool, cmds.size(), cmds.data());
			});

		vkInFlightFence = vktDevice->createFence(false);
		vkRenderFinishedSemaphore = vktDevice->createSemaphore();

		deletionQueue.push_function([=]() {
			vktDevice->destroySyncObjects({ vkInFlightFence , vkRenderFinishedSemaphore });
			});
	}

	void ReaShaderProcessor::cleanupVulkan() {
		vktDevice->waitIdle();
		deletionQueue.flush();
	}
}

