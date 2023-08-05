#include "vktrendering.h"

namespace vkt
{
	namespace Rendering
	{
		Mesh::Mesh(Logical::Device* vktDevice) : vktDevice(vktDevice)
		{
			vktDevice->pDeletionQueue->push_function([=]() { delete (this); });

			// init buffers

			vertexBuffer = new Buffers::AllocatedBuffer(vktDevice);
			indexBuffer = new Buffers::AllocatedBuffer(vktDevice);
		}

		Mesh* Mesh::setVertices(std::vector<Vertex> vertices)
		{
			vertexBuffer->allocate(vertices.size() * sizeof(vkt::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
								   VMA_MEMORY_USAGE_GPU_TO_CPU);
			vertexBuffer->putData(vertices.data(), vertices.size() * sizeof(vkt::Vertex));

			this->vertices = vertices;

			return this;
		}
		Mesh* Mesh::setIndices(std::vector<uint32_t> indices)
		{
			indexBuffer->allocate(indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
								  VMA_MEMORY_USAGE_GPU_TO_CPU);
			indexBuffer->putData(indices.data(), indices.size() * sizeof(uint32_t));

			this->indices = indices;

			return this;
		}

		bool Mesh::load_from_obj(const std::string& filename)
		{

			tinyobj::ObjReaderConfig reader_config;
			tinyobj::ObjReader reader;

			if (!reader.ParseFromFile(filename, reader_config))
			{
				if (!reader.Error().empty())
				{
					std::cerr << "TinyObjReader: " << reader.Error();
				}
				return false;
			}

			if (!reader.Warning().empty())
			{
				std::cout << "TinyObjReader: " << reader.Warning();
			}

			auto& attrib = reader.GetAttrib();
			auto& shapes = reader.GetShapes();
			// auto& materials = reader.GetMaterials();

			//------

			std::unordered_map<Vertex, uint32_t> uniqueVertices{};

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;

			// Loop over shapes
			for (size_t s = 0; s < shapes.size(); s++)
			{
				// Loop over faces(polygon)
				size_t index_offset = 0;
				for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
				{

					size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

					// Loop over vertices in the face.
					for (size_t v = 0; v < fv; v++)
					{

						Vertex new_vert{}; // vertex struct to copy data to

						// access to vertex
						tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

						// vertex position
						tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
						tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
						tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

						new_vert.position.x = vx;
						new_vert.position.y = vy;
						new_vert.position.z = vz;

						// Check if `normal_index` is zero or positive. negative = no normal data
						if (idx.normal_index >= 0)
						{
							tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
							tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
							tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];

							new_vert.normal.x = nx;
							new_vert.normal.y = ny;
							new_vert.normal.z = nz;
						}

						// Check if `texcoord_index` is zero or positive. negative = no texcoord data
						if (idx.texcoord_index >= 0)
						{
							tinyobj::real_t ux = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
							tinyobj::real_t uy = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];

							new_vert.uv.x = ux;
							new_vert.uv.y = 1 - uy;
						}

						// we are setting the vertex color as the vertex normal. This is just for display purposes
						new_vert.color = new_vert.normal;

						if (uniqueVertices.count(new_vert) == 0)
						{
							uniqueVertices[new_vert] = static_cast<uint32_t>(vertices.size());
							vertices.push_back(new_vert);
						}

						indices.push_back(uniqueVertices[new_vert]);
					}
					index_offset += fv;

					// per-face material
					// shapes[s].mesh.material_ids[f];
				}
			}

			setVertices(vertices);
			setIndices(indices);

			return true;
		}
	}
}