#pragma once

#include "vktools.h"
#include "vktdevices.h"
#include "vktbuffers.h"

namespace vkt {

	struct vktDescriptorSet {
		VkDescriptorSet set{ VK_NULL_HANDLE };
		VkDescriptorSetLayout layout{ VK_NULL_HANDLE };
		std::vector<VkDescriptorSetLayoutBinding> bindings;
	};

	class descriptorSetLayoutBuilder : IBuilder<vktDescriptorSet> {
	public:
		descriptorSetLayoutBuilder(const descriptorSetLayoutBuilder&) = delete;
		descriptorSetLayoutBuilder(vktDevice* vktDevice)
			:vktDevice(vktDevice) {
		}
		descriptorSetLayoutBuilder& bind(int binding, VkDescriptorType type, VkShaderStageFlags stages, int descriptorCount = 1) {
			VkDescriptorSetLayoutBinding b = {};
			b.binding = binding;
			b.descriptorCount = descriptorCount;
			b.descriptorType = type;
			b.stageFlags = stages;

			bindings.push_back(b);
			return *this;
		}
		/**
		@return packed struct of {set = null, layout, bindings}
		*/
		vktDescriptorSet build() override {

			VkDescriptorSetLayoutCreateInfo setinfo = {};
			setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			setinfo.pNext = nullptr;

			setinfo.bindingCount = bindings.size();
			//no flags
			setinfo.flags = 0;
			setinfo.pBindings = bindings.data();

			VkDescriptorSetLayout descriptorSetLayout;

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vktDevice->device, &setinfo, nullptr, &descriptorSetLayout));
			VkDevice device = vktDevice->device; // otherwise vktdevice will be nullptr when builder is long gone
			vktDevice->pDeletionQueue->push_function([=]() {
				vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
				});

			return { VK_NULL_HANDLE, descriptorSetLayout, bindings };
		}

	private:
		vktDevice* vktDevice;
		std::vector<VkDescriptorSetLayoutBinding> bindings;
	};

	class vktDescriptorPool {
	public:
		/**
		VkDescriptorPoolSize = { VK_DESCRIPTOR_TYPE_* , int count }
		@param maxSets max descriptor sets count allocatable for this pool
		*/
		vktDescriptorPool(vktDevice* vktDevice, std::vector<VkDescriptorPoolSize> sizes, int maxSets)
			: vktDevice(vktDevice) {

			VkDescriptorPoolCreateInfo pool_info = {};
			pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pool_info.flags = 0;
			pool_info.maxSets = maxSets;
			pool_info.poolSizeCount = (uint32_t)sizes.size();
			pool_info.pPoolSizes = sizes.data();

			VK_CHECK_RESULT(vkCreateDescriptorPool(vktDevice->device, &pool_info, nullptr, &m_descriptorPool));

			// add descriptor set layout to deletion queues
			vktDevice->pDeletionQueue->push_function([=]() {
				vkDestroyDescriptorPool(vktDevice->device, m_descriptorPool, nullptr);
				});
		}

		/**
		Allocates the descriptor sets in batch from a vector of vktDescriptorSets.
		*/
		void allocateDescriptorSets(std::vector<std::reference_wrapper<vktDescriptorSet>> vktDescriptorSets) {

			int count = vktDescriptorSets.size();
			auto layouts = vectors::extract_member_copy_vector<vktDescriptorSet, VkDescriptorSetLayout>(vktDescriptorSets, offsetof(vktDescriptorSet, layout));

			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.pNext = nullptr;
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

			allocInfo.descriptorPool = m_descriptorPool;

			allocInfo.descriptorSetCount = count;

			allocInfo.pSetLayouts = layouts.data();

			auto descriptorSets = vectors::extract_member_copy_vector<vktDescriptorSet, VkDescriptorSet>(vktDescriptorSets, offsetof(vktDescriptorSet, set));

			VK_CHECK_RESULT(vkAllocateDescriptorSets(vktDevice->device, &allocInfo, descriptorSets.data()));

			for (int i = 0; i < count; i++) {
				vktDescriptorSets[i].get().set = descriptorSets[i];
			}
		}

	private:
		vktDevice* vktDevice;
		VkDescriptorPool m_descriptorPool;
	};

	class descriptorSetWriter {
	public:

		descriptorSetWriter(vktDevice* vktDevice)
			: vktDevice(vktDevice) {
		}

		descriptorSetWriter selectDescriptorSet(vktDescriptorSet descriptorSet)
		{
			currentDescSet = descriptorSet;

			currentSetWrite = {};
			currentSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			currentSetWrite.pNext = nullptr;

			currentSetWrite.dstSet = descriptorSet.set;

			return *this;
		}
		descriptorSetWriter selectBinding(int binding)
		{
			currentSetWrite.dstBinding = binding;

			VkDescriptorSetLayoutBinding bindInfo = vkt::vectors::findRef(currentDescSet.bindings,
				[&](const VkDescriptorSetLayoutBinding& b) {
					return b.binding == binding;
				});

			currentSetWrite.descriptorCount = bindInfo.descriptorCount; // the one set in the binding info
			currentSetWrite.descriptorType = bindInfo.descriptorType;

			return *this;
		}

		descriptorSetWriter registerWriteBuffer(vktAllocatedBuffer* aBuffer, size_t size, VkDeviceSize offset) {
			VkDescriptorBufferInfo binfo{};
			binfo.buffer = aBuffer->getBuffer();
			binfo.offset = offset;
			binfo.range = size;

			bufferInfos.push_back(binfo);

			currentSetWrite.pBufferInfo = &bufferInfos.back();

			setWrites.push_back(currentSetWrite);

			return *this;
		}
		descriptorSetWriter registerWriteImage(vktAllocatedImage* aImage, VkSampler sampler, VkImageLayout imageLayout) {

			VkDescriptorImageInfo iInfo{};
			iInfo.sampler = sampler;
			iInfo.imageView = aImage->getImageView();
			iInfo.imageLayout = imageLayout;

			imageInfos.push_back(iInfo);

			currentSetWrite.pImageInfo = &imageInfos.back();

			setWrites.push_back(currentSetWrite);

			return *this;
		}
		void writeRegistered() {
			vkUpdateDescriptorSets(vktDevice->device, setWrites.size(), setWrites.data(), 0, nullptr);
		}

	private:
		std::vector<VkWriteDescriptorSet> setWrites = {};
		vktDescriptorSet currentDescSet{ VK_NULL_HANDLE };
		VkWriteDescriptorSet currentSetWrite{};
		std::vector<VkDescriptorBufferInfo> bufferInfos{};
		std::vector<VkDescriptorImageInfo> imageInfos{};

		vktDevice* vktDevice;
	};

}