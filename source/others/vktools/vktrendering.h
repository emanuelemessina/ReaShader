#pragma once

#include "vktools/vktools.h"
#include "vktools/vktdevices.h"
#include "vktimages.h"
#include "vktdescriptors.h"

#include "tiny_obj_loader.h"


namespace vkt {

	namespace pipeline {

		inline VkFramebuffer createFramebuffer(vktDevice* vktDevice, VkRenderPass renderPass, VkExtent2D extent, std::vector<vktAllocatedImage*> attachments) {

			int count = attachments.size();

			std::vector<VkImageView> views(count);

			for (int i = 0; i < count; i++) {
				views[i] = (attachments[i]->getImageView());
			}

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = count;
			framebufferInfo.pAttachments = views.data();
			framebufferInfo.width = extent.width;
			framebufferInfo.height = extent.height;
			framebufferInfo.layers = 1;

			VkFramebuffer frameBuffer;

			VK_CHECK_RESULT(vkCreateFramebuffer(vktDevice->device, &framebufferInfo, nullptr, &frameBuffer));

			vktDevice->pDeletionQueue->push_function([=]() {
				vkDestroyFramebuffer(vktDevice->device, frameBuffer, nullptr);
				});

			return frameBuffer;
		}

		class renderPassBuilder : IBuilder<VkRenderPass> {
		public:
			renderPassBuilder(const renderPassBuilder&) = delete; // no copy, force passing references or pointers

			renderPassBuilder(vktDevice* vktDevice)
				: vktDevice(vktDevice) {
			}

			renderPassBuilder& addAttachment(VkAttachmentDescription attachDesc, int tag) {
				attachments.push_back(std::move(attachDesc));
				attachmentTags.push_back(tag);
				return *this;
			}
			/**
			Automatically converts src/dst subpass tags to internal indices
			*/
			renderPassBuilder& addSubpassDependency(VkSubpassDependency dependency) {

				dependency.srcSubpass = dependency.srcSubpass == VK_SUBPASS_EXTERNAL ? VK_SUBPASS_EXTERNAL : (uint32_t)vectors::findPos(subpassTags, (int)dependency.srcSubpass);
				dependency.dstSubpass = dependency.dstSubpass == VK_SUBPASS_EXTERNAL ? VK_SUBPASS_EXTERNAL : (uint32_t)vectors::findPos(subpassTags, (int)dependency.dstSubpass);

				dependencies.push_back(dependency);

				return *this;
			}

			class subpassBuilder : IBuilder<VkSubpassDescription> {
			public:

				subpassBuilder& addColorAttachmentRef(int attachmentTag, VkImageLayout layout) {
					colorRefs.push_back({ (uint32_t)vectors::findPos(renderPassAttachmentTags, attachmentTag) ,layout });
					return *this;
				}
				subpassBuilder& preserveAttachments(std::vector<int>& attachmentTags) {
					for (const auto& tag : attachmentTags) {
						preserveRefs.push_back((uint32_t)vectors::findPos(renderPassAttachmentTags, tag));
					}
					return *this;
				}
				subpassBuilder& setDepthStencilRef(int attachmentTag) {

					if (depthRef)
						delete(depthRef);

					depthRef = new VkAttachmentReference();
					depthRef->attachment = (uint32_t)vectors::findPos(renderPassAttachmentTags, attachmentTag);
					depthRef->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					subpass.pDepthStencilAttachment = depthRef;

					return *this;
				}
				renderPassBuilder& endSubpass() {
					subpass.colorAttachmentCount = colorRefs.size();
					subpass.pColorAttachments = colorRefs.data();
					subpass.preserveAttachmentCount = preserveRefs.size();
					subpass.pPreserveAttachments = preserveRefs.data();
					return *caller;
				}

				friend class renderPassBuilder;

				subpassBuilder(const subpassBuilder& s) = delete; // force return by reference
			private:
				~subpassBuilder() {
					if (depthRef)
						delete(depthRef);
				}

				subpassBuilder(renderPassBuilder* caller, std::vector<int>& attachmentTags, VkPipelineBindPoint bindPoint)
					: caller(caller), renderPassAttachmentTags(attachmentTags) {
					subpass = {};
					subpass.pipelineBindPoint = bindPoint;
				}
				VkSubpassDescription build() override {
					return subpass;
				}

				renderPassBuilder* caller;
				VkSubpassDescription subpass;
				std::vector<int> renderPassAttachmentTags;

				std::vector<VkAttachmentReference> colorRefs{};
				std::vector<uint32_t> preserveRefs{};
				VkAttachmentReference* depthRef = nullptr;
			};

			subpassBuilder& initSubpass(VkPipelineBindPoint bindPoint, int tag) {
				subpassBuilders.push_back(new subpassBuilder(this, attachmentTags, bindPoint));
				subpassTags.push_back(tag);
				return *(subpassBuilders.back());
			}
			// TODO call destructors on subpassbuilders

			VkRenderPass build() override {

				VkRenderPassCreateInfo renderPassInfo{};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				renderPassInfo.attachmentCount = attachments.size();
				renderPassInfo.pAttachments = attachments.data();

				int subpassCount = subpassBuilders.size();
				std::vector<VkSubpassDescription> subpasses(subpassCount);
				for (int i = 0; i < subpassCount; i++) {
					subpasses[i] = std::move(subpassBuilders[i]->build());
				}

				renderPassInfo.subpassCount = subpassCount;
				renderPassInfo.pSubpasses = subpasses.data();

				renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
				renderPassInfo.pDependencies = dependencies.data();

				VkRenderPass renderPass;
				VK_CHECK_RESULT(vkCreateRenderPass(vktDevice->device, &renderPassInfo, nullptr, &renderPass));

				for (auto subpass : subpassBuilders) {
					delete subpass;
				}

				VkDevice device = vktDevice->device;
				vktDevice->pDeletionQueue->push_function([=]() {
					vkDestroyRenderPass(device, renderPass, nullptr);
					});

				return renderPass;
			}

		private:
			vktDevice* vktDevice;

			std::vector<VkSubpassDependency> dependencies{};

			std::vector<int> attachmentTags{};
			std::vector<VkAttachmentDescription> attachments{};

			std::vector<int> subpassTags{};
			std::vector<subpassBuilder*> subpassBuilders{};
		};

		inline VkShaderModule createShaderModule(vktDevice* vktDevice, const char* spvPath) {
			const std::vector<char> code = io::readFile(spvPath);

			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = code.size();
			createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

			VkShaderModule shaderModule;
			if (vkCreateShaderModule(vktDevice->device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			return shaderModule;
		}
	};

	namespace textures {
		inline VkSampler createSampler(vktDevice* vktDevice, VkFilter filters, VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT) {
			VkSamplerCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			info.pNext = nullptr;

			info.magFilter = filters;
			info.minFilter = filters;
			info.addressModeU = samplerAddressMode;
			info.addressModeV = samplerAddressMode;
			info.addressModeW = samplerAddressMode;

			VkSampler sampler;

			VK_CHECK_RESULT(vkCreateSampler(vktDevice->device, &info, nullptr, &sampler));

			vktDevice->pDeletionQueue->push_function([=]() {
				vkDestroySampler(vktDevice->device, sampler, nullptr);
				});

			return sampler;
		}
	};

	struct VertexInputDescription {

		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;

		VkPipelineVertexInputStateCreateFlags flags = 0;
	};

	struct Vertex {

		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
		glm::vec2 uv;

		static VertexInputDescription get_vertex_description()
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

		bool operator==(const Vertex& other) const {
			return position == other.position && uv == other.uv && normal == other.normal;
		}
	};
}

// hash func for vertex

namespace std {
	template<>
	struct hash<vkt::Vertex> {
		size_t operator()(vkt::Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.position) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.uv) << 1);
		}
	};
}

namespace vkt {

	class Mesh {
	public:
		Mesh(vktDevice* vktDevice) :vktDevice(vktDevice) {
			vktDevice->pDeletionQueue->push_function([=]() {
				delete(this);
				});

			// init buffers

			vertexBuffer = new vktAllocatedBuffer(vktDevice);
			indexBuffer = new vktAllocatedBuffer(vktDevice);
		}

		Mesh* setVertices(std::vector<Vertex> vertices) {
			vertexBuffer->allocate(vertices.size() * sizeof(vkt::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
			vertexBuffer->putData(vertices.data(), vertices.size() * sizeof(vkt::Vertex));

			this->vertices = vertices;

			return this;
		}
		Mesh* setIndices(std::vector<uint32_t> indices) {
			indexBuffer->allocate(indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
			indexBuffer->putData(indices.data(), indices.size() * sizeof(uint32_t));

			this->indices = indices;

			return this;
		}

		/**
		All objects will get merged into one Mesh object.
		*/
		bool load_from_obj(const char* filename) {

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

			std::unordered_map<Vertex, uint32_t> uniqueVertices{};

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;

			// Loop over shapes
			for (size_t s = 0; s < shapes.size(); s++) {
				// Loop over faces(polygon)
				size_t index_offset = 0;
				for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {

					size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

					// Loop over vertices in the face.
					for (size_t v = 0; v < fv; v++) {

						Vertex new_vert{}; // vertex struct to copy data to

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

						if (uniqueVertices.count(new_vert) == 0) {
							uniqueVertices[new_vert] = static_cast<uint32_t>(vertices.size());
							vertices.push_back(new_vert);
						}

						indices.push_back(uniqueVertices[new_vert]);
					}
					index_offset += fv;

					// per-face material
					shapes[s].mesh.material_ids[f];
				}
			}

			setVertices(vertices);
			setIndices(indices);

			return true;
		}

		vktAllocatedBuffer* getVertexBuffer() { return vertexBuffer; }
		vktAllocatedBuffer* getIndexBuffer() { return indexBuffer; }

		std::vector<Vertex> getVertices() { return vertices; }
		std::vector<uint32_t> getIndices() { return indices; }

	private:
		vktDevice* vktDevice = nullptr;
		vktAllocatedBuffer* vertexBuffer = nullptr;
		vktAllocatedBuffer* indexBuffer = nullptr;

		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
	};

	struct Material {
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;
		VkPipelineCache pipelineCache;

		vktDescriptorSet* textureSet;
	};

	struct RenderObject {
		Mesh* mesh;

		Material* material;

		glm::mat4 transformMatrix;
	};
}
