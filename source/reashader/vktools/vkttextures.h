#pragma once

#include "vktdevices.h"

namespace vkt
{
	namespace textures
	{
		inline VkSampler createSampler(Logical::Device* vktDevice, VkFilter filters,
									   VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT)
		{
			VkSamplerCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			info.pNext = nullptr;

			info.magFilter = filters;
			info.minFilter = filters;
			info.addressModeU = samplerAddressMode;
			info.addressModeV = samplerAddressMode;
			info.addressModeW = samplerAddressMode;

			VkSampler sampler;

			VK_CHECK_RESULT(vkCreateSampler(vktDevice->vk(), &info, nullptr, &sampler));

			vktDevice->pDeletionQueue->push_function([=]() { vkDestroySampler(vktDevice->vk(), sampler, nullptr); });

			return sampler;
		}
	}; // namespace textures

}