/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "vktdescriptors.h"

namespace vkt
{

	namespace Descriptors
	{
		// DescriptorSetLayoutBuilder

		DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::bind(int binding, VkDescriptorType type,
																	 VkShaderStageFlags stages, int descriptorCount)
		{
			VkDescriptorSetLayoutBinding b = {};
			b.binding = binding;
			b.descriptorCount = descriptorCount;
			b.descriptorType = type;
			b.stageFlags = stages;

			bindings.push_back(b);
			return *this;
		}

		DescriptorSet DescriptorSetLayoutBuilder::build()
		{

			VkDescriptorSetLayoutCreateInfo setinfo = {};
			setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			setinfo.pNext = nullptr;

			setinfo.bindingCount = static_cast<uint32_t>(bindings.size());
			// no flags
			setinfo.flags = 0;
			setinfo.pBindings = bindings.data();

			VkDescriptorSetLayout descriptorSetLayout;

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vktDevice->vk(), &setinfo, nullptr, &descriptorSetLayout));
			VkDevice device = vktDevice->vk(); // otherwise vktdevice will be nullptr when builder is long gone
			vktDevice->pDeletionQueue->push_function(
				[=]() { vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr); });

			return { VK_NULL_HANDLE, descriptorSetLayout, bindings };
		}

		//--------------------------------------------

		// DescriptorPool

		DescriptorPool::DescriptorPool(Logical::Device* vktDevice, std::vector<VkDescriptorPoolSize> sizes, int maxSets)
			: vktDevice(vktDevice)
		{

			VkDescriptorPoolCreateInfo pool_info = {};
			pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pool_info.flags = 0;
			pool_info.maxSets = maxSets;
			pool_info.poolSizeCount = (uint32_t)sizes.size();
			pool_info.pPoolSizes = sizes.data();

			VK_CHECK_RESULT(vkCreateDescriptorPool(vktDevice->vk(), &pool_info, nullptr, &m_descriptorPool));

			// add descriptor set layout to deletion queues
			vktDevice->pDeletionQueue->push_function(
				[=]() { vkDestroyDescriptorPool(vktDevice->vk(), m_descriptorPool, nullptr); });
		}

		void DescriptorPool::allocateDescriptorSets(
			std::vector<std::reference_wrapper<DescriptorSet>> vktDescriptorSets)
		{

			uint32_t count = static_cast<uint32_t>(vktDescriptorSets.size());
			auto layouts = vectors::extract_member_copy_vector<DescriptorSet, VkDescriptorSetLayout>(
				vktDescriptorSets, offsetof(DescriptorSet, layout));

			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.pNext = nullptr;
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

			allocInfo.descriptorPool = m_descriptorPool;

			allocInfo.descriptorSetCount = count;

			allocInfo.pSetLayouts = layouts.data();

			auto descriptorSets = vectors::extract_member_copy_vector<DescriptorSet, VkDescriptorSet>(
				vktDescriptorSets, offsetof(DescriptorSet, set));

			VK_CHECK_RESULT(vkAllocateDescriptorSets(vktDevice->vk(), &allocInfo, descriptorSets.data()));

			for (uint32_t i = 0; i < count; i++)
			{
				vktDescriptorSets[i].get().set = descriptorSets[i];
			}
		}

		// DescriptorSetWriter

		DescriptorSetWriter DescriptorSetWriter::selectDescriptorSet(DescriptorSet descriptorSet)
		{
			currentDescSet = descriptorSet;

			currentSetWrite = {};
			currentSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			currentSetWrite.pNext = nullptr;

			currentSetWrite.dstSet = descriptorSet.set;

			return *this;
		}
		DescriptorSetWriter DescriptorSetWriter::selectBinding(int binding)
		{
			currentSetWrite.dstBinding = binding;

			VkDescriptorSetLayoutBinding bindInfo = vectors::findRef(
				currentDescSet.bindings, [&](const VkDescriptorSetLayoutBinding& b) { return b.binding == binding; });

			currentSetWrite.descriptorCount = bindInfo.descriptorCount; // the one set in the binding info
			currentSetWrite.descriptorType = bindInfo.descriptorType;

			return *this;
		}

		DescriptorSetWriter DescriptorSetWriter::registerWriteBuffer(Buffers::AllocatedBuffer* aBuffer, size_t size,
																	 VkDeviceSize offset)
		{
			VkDescriptorBufferInfo binfo{};
			binfo.buffer = aBuffer->getBuffer();
			binfo.offset = offset;
			binfo.range = size;

			bufferInfos.push_back(binfo);

			currentSetWrite.pBufferInfo = &bufferInfos.back();

			setWrites.push_back(currentSetWrite);

			return *this;
		}
		DescriptorSetWriter DescriptorSetWriter::registerWriteImage(Images::AllocatedImage* aImage, VkSampler sampler,
																	VkImageLayout imageLayout)
		{

			VkDescriptorImageInfo iInfo{};
			iInfo.sampler = sampler;
			iInfo.imageView = aImage->getImageView();
			iInfo.imageLayout = imageLayout;

			imageInfos.push_back(iInfo);

			currentSetWrite.pImageInfo = &imageInfos.back();

			setWrites.push_back(currentSetWrite);

			return *this;
		}
		void DescriptorSetWriter::writeRegistered()
		{
			vkUpdateDescriptorSets(vktDevice->vk(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
								   nullptr);
		}

	} // namespace Descriptors
} // namespace vkt