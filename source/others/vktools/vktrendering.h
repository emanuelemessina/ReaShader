#pragma once

#include "vktools/vktools.h"
#include "vktools/vktdevices.h"
#include "vktimages.h"

namespace vkt {

	struct AllocatedBuffer {
		VkBuffer buffer;
		VmaAllocation allocation;

		void createBuffer(vktDevice* vktDevice, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, bool pushToDeletionQueue = true)
		{
			//allocate vertex buffer
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.pNext = nullptr;

			bufferInfo.size = allocSize;
			bufferInfo.usage = usage;

			VmaAllocationCreateInfo vmaallocInfo = {};
			vmaallocInfo.usage = memoryUsage;

			//allocate the buffer
			VK_CHECK_RESULT(vmaCreateBuffer(vktDevice->vmaAllocator, &bufferInfo, &vmaallocInfo,
				&(this->buffer),
				&(this->allocation),
				nullptr));

			this->vktDevice = vktDevice;

			if (pushToDeletionQueue)
				vktDevice->pDeletionQueue->push_function([=]() {
				destroy();
					});
		}

		void destroy() {
			vmaDestroyBuffer(vktDevice->vmaAllocator, this->buffer, this->allocation);
			VK_CHECK_RESULT(vmaFlushAllocation(vktDevice->vmaAllocator, this->allocation, 0, VK_WHOLE_SIZE));
		}

	private:
		vktDevice* vktDevice;
	};

	void writeDescriptorSet(vktDevice* vktDevice, VkDescriptorSet descriptorSet, VkDescriptorType descriptorType, AllocatedBuffer aBuffer, size_t size, int offset, int dstBinding);

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

		static VertexInputDescription get_vertex_description();
	};

	class Mesh {
	public:
		Mesh(vktDevice* vktDevice) :vktDevice(vktDevice) {
			vktDevice->pDeletionQueue->push_function([=]() {
				delete(this);
				});
		}
		~Mesh() {};

		std::vector<Vertex> vertices;

		AllocatedBuffer vertexBuffer;

		bool load_from_obj(const char* filename);

		void verticesToBuffer();

	private:
		vktDevice* vktDevice = nullptr;
	};

	struct Material {
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;
		VkPipelineCache pipelineCache;

		VkDescriptorSet textureSet{ VK_NULL_HANDLE };
	};

	struct RenderObject {
		Mesh* mesh;

		Material* material;

		glm::mat4 transformMatrix;
	};

	VkDescriptorSetLayoutBinding createDescriptorSetLayoutBinding(int binding, VkDescriptorType type, VkShaderStageFlags stages);

	class descriptorSetWriter {
	public:
		descriptorSetWriter(vktDevice* vktDevice, VkDescriptorSet descriptorSet, VkDescriptorType descriptorType, int dstBinding)
			: vktDevice(vktDevice) {
			setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			setWrite.pNext = nullptr;

			setWrite.dstBinding = dstBinding;
			setWrite.dstSet = descriptorSet;

			setWrite.descriptorCount = 1;
			setWrite.descriptorType = descriptorType;
		}
		void writeBuffer(AllocatedBuffer aBuffer, size_t size, int offset) {
			VkDescriptorBufferInfo binfo;
			binfo.buffer = aBuffer.buffer;
			binfo.offset = offset;
			binfo.range = size;

			setWrite.pBufferInfo = &binfo;

			write();
		}
		void writeImage(AllocatedImage* aImage, VkSampler sampler, VkImageLayout imageLayout) {

			VkDescriptorImageInfo iInfo;
			iInfo.sampler = sampler;
			iInfo.imageView = aImage->imageView;
			iInfo.imageLayout = imageLayout;

			setWrite.pImageInfo = &iInfo;

			write();
		}

	private:
		VkWriteDescriptorSet setWrite = {};
		vktDevice* vktDevice;
		void write() {
			vkUpdateDescriptorSets(vktDevice->device, 1, &setWrite, 0, nullptr);
		}
	};

}