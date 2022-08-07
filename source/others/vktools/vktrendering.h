#pragma once

#include "vktools.h"

namespace vkt {

	struct VertexInputDescription {

		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;

		VkPipelineVertexInputStateCreateFlags flags = 0;
	};

	struct Vertex {

		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;

		static VertexInputDescription get_vertex_description();
	};

	struct Mesh {
		std::vector<Vertex> vertices;

		AllocatedBuffer vertexBuffer;

		bool load_from_obj(const char* filename);
	};

	struct Material {
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;
	};

	struct RenderObject {
		Mesh* mesh;

		Material* material;

		glm::mat4 transformMatrix;
	};


}