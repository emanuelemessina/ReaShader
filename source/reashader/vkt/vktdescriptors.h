#pragma once

#include "vktbuffers.h"
#include "vktcommon.h"
#include "vktdevices.h"
#include "vktimages.h"

namespace vkt
{

	namespace Descriptors
	{
		struct DescriptorSet
		{
			VkDescriptorSet set{ VK_NULL_HANDLE };
			VkDescriptorSetLayout layout{ VK_NULL_HANDLE };
			std::vector<VkDescriptorSetLayoutBinding> bindings;
		};

		class DescriptorSetLayoutBuilder : IBuilder<DescriptorSet>
		{
		  public:
			DescriptorSetLayoutBuilder(const DescriptorSetLayoutBuilder&) = delete;
			DescriptorSetLayoutBuilder(Logical::Device* vktDevice) : vktDevice(vktDevice)
			{
			}
			DescriptorSetLayoutBuilder& bind(int binding, VkDescriptorType type, VkShaderStageFlags stages,
											 int descriptorCount = 1);

			/**
			@return packed struct of {set = null, layout, bindings}
			*/
			DescriptorSet build() override;

		  private:
			Logical::Device* vktDevice;
			std::vector<VkDescriptorSetLayoutBinding> bindings;
		};

		class DescriptorPool : IVkWrapper<VkDescriptorPool>
		{
		  public:
			/**
			VkDescriptorPoolSize = { VK_DESCRIPTOR_TYPE_* , int count }
			@param maxSets max descriptor sets count allocatable for this pool
			*/
			DescriptorPool(Logical::Device* vktDevice, std::vector<VkDescriptorPoolSize> sizes, int maxSets);

			/**
			Allocates the descriptor sets in batch from a vector of vktDescriptorSets.
			*/
			void allocateDescriptorSets(std::vector<std::reference_wrapper<DescriptorSet>> vktDescriptorSets);

			VkDescriptorPool vk()
			{
				return m_descriptorPool;
			}

		  private:
			Logical::Device* vktDevice;
			VkDescriptorPool m_descriptorPool;
		};

		class DescriptorSetWriter
		{
		  public:
			DescriptorSetWriter(Logical::Device* vktDevice) : vktDevice(vktDevice)
			{
			}

			DescriptorSetWriter selectDescriptorSet(DescriptorSet descriptorSet);
			DescriptorSetWriter selectBinding(int binding);

			DescriptorSetWriter registerWriteBuffer(Buffers::AllocatedBuffer* aBuffer, size_t size,
													VkDeviceSize offset);
			DescriptorSetWriter registerWriteImage(Images::AllocatedImage* aImage, VkSampler sampler,
												   VkImageLayout imageLayout);
			void writeRegistered();

		  private:
			std::vector<VkWriteDescriptorSet> setWrites = {};
			DescriptorSet currentDescSet{ VK_NULL_HANDLE };
			VkWriteDescriptorSet currentSetWrite{};
			std::vector<VkDescriptorBufferInfo> bufferInfos{};
			std::vector<VkDescriptorImageInfo> imageInfos{};

			Logical::Device* vktDevice;
		};
	} // namespace Descriptors
} // namespace vkt