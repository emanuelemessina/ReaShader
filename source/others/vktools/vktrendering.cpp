#include "vktools/vktrendering.h"

#include "tiny_obj_loader.h"

namespace vkt {

	void writeDescriptorSet(vktDevice* vktDevice, VkDescriptorSet descriptorSet, VkDescriptorType descriptorType, AllocatedBuffer aBuffer, size_t size, int offset, int dstBinding) {
		//information about the buffer we want to point at in the descriptor
		VkDescriptorBufferInfo binfo;
		binfo.buffer = aBuffer.buffer;
		binfo.offset = offset;
		binfo.range = size;

		VkWriteDescriptorSet setWrite = {};
		setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		setWrite.pNext = nullptr;

		setWrite.dstBinding = dstBinding;
		setWrite.dstSet = descriptorSet;

		setWrite.descriptorCount = 1;
		setWrite.descriptorType = descriptorType;
		setWrite.pBufferInfo = &binfo;

		vkUpdateDescriptorSets(vktDevice->device, 1, &setWrite, 0, nullptr);
	}

	VkDescriptorSetLayoutBinding createDescriptorSetLayoutBinding(int binding, VkDescriptorType type, VkShaderStageFlags stages) {
		VkDescriptorSetLayoutBinding b = {};
		b.binding = binding;
		b.descriptorCount = 1;
		b.descriptorType = type;
		b.stageFlags = stages;
		return b;
	}

	VertexInputDescription Vertex::get_vertex_description()
	{
		VertexInputDescription description;

		//we will have just 1 vertex buffer binding, with a per-vertex rate
		VkVertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = sizeof(Vertex);
		mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		description.bindings.push_back(mainBinding);

		//Position will be stored at Location 0
		VkVertexInputAttributeDescription positionAttribute = {};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = offsetof(Vertex, position);

		//Normal will be stored at Location 1
		VkVertexInputAttributeDescription normalAttribute = {};
		normalAttribute.binding = 0;
		normalAttribute.location = 1;
		normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		normalAttribute.offset = offsetof(Vertex, normal);

		//Color will be stored at Location 2
		VkVertexInputAttributeDescription colorAttribute = {};
		colorAttribute.binding = 0;
		colorAttribute.location = 2;
		colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		colorAttribute.offset = offsetof(Vertex, color);

		//UV will be stored at Location 3
		VkVertexInputAttributeDescription uvAttribute = {};
		uvAttribute.binding = 0;
		uvAttribute.location = 3;
		uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
		uvAttribute.offset = offsetof(Vertex, uv);

		description.attributes.push_back(positionAttribute);
		description.attributes.push_back(normalAttribute);
		description.attributes.push_back(colorAttribute);
		description.attributes.push_back(uvAttribute);

		return description;
	}

	/**
	All objects will get merged into one Mesh object. The object must be triangulated.
	*/
	bool Mesh::load_from_obj(const char* filename) {

		tinyobj::ObjReaderConfig reader_config;
		tinyobj::ObjReader reader;

		if (!reader.ParseFromFile(filename, reader_config)) {
			if (!reader.Error().empty()) {
				std::cerr << "TinyObjReader: " << reader.Error();
			}
			return false;
		}

		if (!reader.Warning().empty()) {
			std::cout << "TinyObjReader: " << reader.Warning();
		}

		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();
		auto& materials = reader.GetMaterials();

		//------

		// Loop over shapes
		for (size_t s = 0; s < shapes.size(); s++) {
			// Loop over faces(polygon)
			size_t index_offset = 0;
			for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {

				size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

				// Loop over vertices in the face.
				for (size_t v = 0; v < fv; v++) {

					Vertex new_vert; // vertex struct to copy data to

					// access to vertex
					tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

					//vertex position
					tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
					tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
					tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

					new_vert.position.x = vx;
					new_vert.position.y = vy;
					new_vert.position.z = vz;

					// Check if `normal_index` is zero or positive. negative = no normal data
					if (idx.normal_index >= 0) {
						tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
						tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
						tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];

						new_vert.normal.x = nx;
						new_vert.normal.y = ny;
						new_vert.normal.z = nz;
					}

					// Check if `texcoord_index` is zero or positive. negative = no texcoord data
					if (idx.texcoord_index >= 0) {
						tinyobj::real_t ux = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
						tinyobj::real_t uy = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];

						new_vert.uv.x = ux;
						new_vert.uv.y = 1 - uy;
					}

					//we are setting the vertex color as the vertex normal. This is just for display purposes
					new_vert.color = new_vert.normal;

					vertices.push_back(new_vert);
				}
				index_offset += fv;

				// per-face material
				shapes[s].mesh.material_ids[f];
			}
		}

		verticesToBuffer();

		return true;
	}

	/**
	Copy vertex data to allocated buffer.
	Use this if manually setting the vertices (eg. not using load_from_obj).
	*/
	void Mesh::verticesToBuffer() {
		//allocate vertex buffer
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		//this is the total size, in bytes, of the buffer we are allocating
		bufferInfo.size = vertices.size() * sizeof(vkt::Vertex);
		//this buffer is going to be used as a Vertex Buffer
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VmaAllocationCreateInfo vmaallocInfo = {};
		vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

		//allocate the buffer
		VK_CHECK_RESULT(vmaCreateBuffer(vktDevice->vmaAllocator, &bufferInfo, &vmaallocInfo,
			&(vertexBuffer.buffer),
			&(vertexBuffer.allocation),
			nullptr));

		//copy vertex data
		void* data;
		vmaMapMemory(vktDevice->vmaAllocator, vertexBuffer.allocation, &data);

		memcpy(data, vertices.data(), vertices.size() * sizeof(vkt::Vertex));

		vmaUnmapMemory(vktDevice->vmaAllocator, vertexBuffer.allocation);

		vktDevice->pDeletionQueue->push_function([=]() {
			vmaDestroyBuffer(vktDevice->vmaAllocator, vertexBuffer.buffer, vertexBuffer.allocation);
			});
	}
}