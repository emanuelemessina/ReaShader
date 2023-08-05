#pragma once

#include "vktcommon.h"
#include "vktdescriptors.h"
#include "vktimages.h"
#include "vktdevices.h"
#include "vktvertex.h"
#include "vktbuffers.h"

#include "tiny_obj_loader.h"

namespace vkt
{
	namespace Rendering
	{
		class Mesh
		{
		  public:
			Mesh(Logical::Device* vktDevice);

			Mesh* setVertices(std::vector<Vertex> vertices);
			Mesh* setIndices(std::vector<uint32_t> indices);

			/**
			All objects will get merged into one Mesh object.
			*/
			bool load_from_obj(const std::string& filename);

			Buffers::AllocatedBuffer* getVertexBuffer()
			{
				return vertexBuffer;
			}
			Buffers::AllocatedBuffer* getIndexBuffer()
			{
				return indexBuffer;
			}

			std::vector<Vertex> getVertices()
			{
				return vertices;
			}
			std::vector<uint32_t> getIndices()
			{
				return indices;
			}

		  private:
			Logical::Device* vktDevice = nullptr;
			Buffers::AllocatedBuffer* vertexBuffer = nullptr;
			Buffers::AllocatedBuffer* indexBuffer = nullptr;

			std::vector<Vertex> vertices{};
			std::vector<uint32_t> indices{};
		};

		struct Material
		{
			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkPipelineCache pipelineCache;

			Descriptors::DescriptorSet* textureSet;
		};

		struct RenderObject
		{
			Mesh* mesh = nullptr;

			Material* material;

			glm::mat4 localTransformMatrix = glm::mat4{ 1.0f };
		};
	}

} // namespace vkt
