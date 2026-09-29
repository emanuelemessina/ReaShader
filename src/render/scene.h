/**
 * @file
 * @brief gpu::Scene: textured meshes drawn over the frame (the logo).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"

#include <glm/mat4x4.hpp>

#include <filesystem>
#include <vector>

namespace ReaShader::gpu
{
	// Mesh vertex, as read by scene.vert
	struct Vertex
	{
		float position[3];
		float normal[3];
		float uv[2];
	};

	// Triangles from an .obj file
	struct Mesh
	{
		void load(Context& context, const std::filesystem::path& objFile);
		void destroy(Context& context);

		Buffer vertices;
		Buffer indices;
		uint32_t indexCount = 0;
	};

	// An image file (png, jpg, ...) on the GPU
	struct Texture
	{
		void load(Context& context, const std::filesystem::path& imageFile);
		void destroy(Context& context);

		Image image;
	};

	// A small 3D scene drawn over the frame: textured meshes, one camera, a depth buffer.
	// Its one object today is the spinning ReaShader logo (the plugin's easter egg).
	class Scene
	{
	  public:
		void create(Context& context);
		void destroy(Context& context);

		// (re)creates the depth buffer for the frame size; call before recording
		void prepare(Context& context, VkExtent2D extent);

		// draws over `target` (a color attachment holding the frame so far)
		void record(VkCommandBuffer commandBuffer, const Image& target, double time, double frameRate);

	  private:
		struct Object
		{
			const Mesh* mesh;
			VkDescriptorSet textureSet; // the object's texture, as `albedo`
			glm::mat4 localTransform;
		};

		VkDescriptorSet createTextureSet(Context& context, const Texture& texture);

		Mesh logoMesh;
		Texture logoTexture;
		std::vector<Object> objects;

		Image depth;

		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
	};
} // namespace ReaShader::gpu
