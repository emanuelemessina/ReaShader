/**
 * @file
 * @brief gpu::Scene: textured meshes drawn over the frame (the logo).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

// Vulkan clip space: depth 0..1
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "render/scene.h"

#include "util/paths.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <cmath>
#include <cstddef>
#include <cstring>

namespace ReaShader::gpu
{
	namespace
	{
		// built by glslc from src/shaders/internal/scene.*
		constexpr uint32_t kSceneVertexSpirv[] = {
#include "scene.vert.inc"
		};
		constexpr uint32_t kSceneFragmentSpirv[] = {
#include "scene.frag.inc"
		};

		constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

		// host-visible buffer filled with `bytes`
		void createFilledBuffer(Context& context, Buffer& buffer, const void* data, size_t bytes,
								VkBufferUsageFlags usage)
		{
			buffer.create(context.allocator, bytes, usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
			std::memcpy(buffer.mapped, data, bytes);
			buffer.flush(context.allocator);
		}
	} // namespace

	// -------- Mesh --------

	void Mesh::load(Context& context, const std::filesystem::path& objFile)
	{
		tinyobj::ObjReader reader;
		if (!reader.ParseFromFile(objFile.string()))
			throw std::runtime_error("Can't load mesh " + objFile.string() + ": " + reader.Error());

		const tinyobj::attrib_t& attrib = reader.GetAttrib();
		std::vector<Vertex> vertexData;
		std::vector<uint32_t> indexData;

		// one vertex per face corner (the .obj is triangulated on load)
		for (const tinyobj::shape_t& shape : reader.GetShapes())
		{
			for (const tinyobj::index_t& corner : shape.mesh.indices)
			{
				Vertex vertex{};
				for (int i = 0; i < 3; i++)
					vertex.position[i] = attrib.vertices[3 * (size_t)corner.vertex_index + (size_t)i];
				if (corner.normal_index >= 0)
				{
					for (int i = 0; i < 3; i++)
						vertex.normal[i] = attrib.normals[3 * (size_t)corner.normal_index + (size_t)i];
				}
				if (corner.texcoord_index >= 0)
				{
					vertex.uv[0] = attrib.texcoords[2 * (size_t)corner.texcoord_index];
					vertex.uv[1] = 1.0f - attrib.texcoords[2 * (size_t)corner.texcoord_index + 1]; // .obj: v up
				}
				indexData.push_back((uint32_t)vertexData.size());
				vertexData.push_back(vertex);
			}
		}

		createFilledBuffer(context, vertices, vertexData.data(), vertexData.size() * sizeof(Vertex),
						   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		createFilledBuffer(context, indices, indexData.data(), indexData.size() * sizeof(uint32_t),
						   VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
		indexCount = (uint32_t)indexData.size();
	}

	void Mesh::destroy(Context& context)
	{
		vertices.destroy(context.allocator);
		indices.destroy(context.allocator);
		indexCount = 0;
	}

	// -------- Texture --------

	void Texture::load(Context& context, const std::filesystem::path& imageFile)
	{
		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load(imageFile.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels)
			throw std::runtime_error("Can't load texture " + imageFile.string());

		Buffer staging;
		createFilledBuffer(context, staging, pixels, (size_t)width * (size_t)height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		stbi_image_free(pixels);

		VkExtent2D extent{ (uint32_t)width, (uint32_t)height };
		image.create(context.device, context.allocator, extent, VK_FORMAT_R8G8B8A8_SRGB,
					 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		VkCommandBuffer commandBuffer = context.beginCommands();
		transition(commandBuffer, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
				   VK_ACCESS_2_TRANSFER_WRITE_BIT);
		VkBufferImageCopy region{};
		region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageExtent = { extent.width, extent.height, 1 };
		vkCmdCopyBufferToImage(commandBuffer, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
							   &region);
		transition(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
				   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		context.submitAndWait();

		staging.destroy(context.allocator);
	}

	void Texture::destroy(Context& context)
	{
		image.destroy(context.device, context.allocator);
	}

	// -------- Scene --------

	void Scene::create(Context& context)
	{
		VkDevice device = context.device;

		// resources
		std::filesystem::path resources = util::paths::resourcesDir();
		logoMesh.load(context, resources / "meshes" / "reashader.obj");
		logoTexture.load(context, resources / "images" / "reashader-logo-hr.png");

		// descriptors: binding 0 = the object's texture
		VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
											  VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = 1;
		setLayoutInfo.pBindings = &binding;
		VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout));

		// push constants: the object's model-view-projection matrix
		VkPushConstantRange pushConstants{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &setLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstants;
		VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		// pipeline: textured meshes with depth test
		VkVertexInputBindingDescription vertexBinding{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription attributes[] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
			{ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
		};
		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &vertexBinding;
		vertexInput.vertexAttributeDescriptionCount = 3;
		vertexInput.pVertexAttributeDescriptions = attributes;

		PipelineDesc pipelineDesc;
		pipelineDesc.vertexSpirv = kSceneVertexSpirv;
		pipelineDesc.vertexWords = std::size(kSceneVertexSpirv);
		pipelineDesc.fragmentSpirv = kSceneFragmentSpirv;
		pipelineDesc.fragmentWords = std::size(kSceneFragmentSpirv);
		pipelineDesc.layout = pipelineLayout;
		pipelineDesc.vertexInput = &vertexInput;
		pipelineDesc.depthFormat = kDepthFormat;
		pipeline = createPipeline(device, pipelineDesc);

		// objects: the logo, stood up (the mesh lies on its back)
		glm::mat4 standUp = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		objects.push_back({ &logoMesh, createTextureSet(context, logoTexture), standUp });
	}

	VkDescriptorSet Scene::createTextureSet(Context& context, const Texture& texture)
	{
		VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		setInfo.descriptorPool = context.descriptorPool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &setLayout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		VK_CHECK(vkAllocateDescriptorSets(context.device, &setInfo, &set));

		VkDescriptorImageInfo imageInfo{ context.sampler, texture.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = set;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
		return set;
	}

	void Scene::destroy(Context& context)
	{
		VkDevice device = context.device;
		for (const Object& object : objects)
			vkFreeDescriptorSets(device, context.descriptorPool, 1, &object.textureSet);
		objects.clear();
		if (pipeline)
			vkDestroyPipeline(device, pipeline, nullptr);
		if (pipelineLayout)
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		if (setLayout)
			vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		depth.destroy(device, context.allocator);
		logoTexture.destroy(context);
		logoMesh.destroy(context);
		pipeline = VK_NULL_HANDLE;
		pipelineLayout = VK_NULL_HANDLE;
		setLayout = VK_NULL_HANDLE;
	}

	void Scene::prepare(Context& context, VkExtent2D extent)
	{
		if (depth.image && depth.extent.width == extent.width && depth.extent.height == extent.height)
			return;
		depth.destroy(context.device, context.allocator);
		depth.create(context.device, context.allocator, extent, kDepthFormat,
					 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
	}

	void Scene::record(VkCommandBuffer commandBuffer, const Image& target, double time, double frameRate)
	{
		// the target already holds the frame: wait for its writes, then draw over it
		transition(commandBuffer, target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
		transition(commandBuffer, depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

		VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = target.view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttachment.imageView = depth.view;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.renderArea = { { 0, 0 }, target.extent };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthAttachment;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		VkViewport viewport{ 0, 0, (float)target.extent.width, (float)target.extent.height, 0, 1 };
		VkRect2D scissor{ { 0, 0 }, target.extent };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// camera: 5 units back, 70 degrees field of view; y flipped for Vulkan's clip space
		float aspect = (float)target.extent.width / (float)target.extent.height;
		glm::mat4 projection = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 200.0f);
		projection[1][1] *= -1.0f;
		glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));

		// spin: one degree per video frame, wobbling over time
		float angle = glm::radians((float)(time * frameRate));
		glm::vec3 axis(0.1f * std::sin((float)time), 1.0f, 0.05f * std::cos((float)time));
		glm::mat4 spin = glm::rotate(glm::mat4(1.0f), angle, glm::normalize(axis));

		for (const Object& object : objects)
		{
			glm::mat4 modelViewProjection = projection * view * spin * object.localTransform;
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
							   glm::value_ptr(modelViewProjection));
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
									&object.textureSet, 0, nullptr);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &object.mesh->vertices.buffer, &offset);
			vkCmdBindIndexBuffer(commandBuffer, object.mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffer, object.mesh->indexCount, 1, 0, 0, 0);
		}

		vkCmdEndRendering(commandBuffer);
	}
} // namespace ReaShader::gpu
