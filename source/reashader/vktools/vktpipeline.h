#pragma once

#include "vktdevices.h"
#include "vktimages.h"

namespace vkt
{
	namespace Pipeline
	{

		inline VkFramebuffer createFramebuffer(Logical::Device* vktDevice, deletion_queue* pDeletionQueue,
											   VkRenderPass renderPass, VkExtent2D extent,
											   std::vector<Images::AllocatedImage*> attachments)
		{

			uint32_t count = static_cast<uint32_t>(attachments.size());

			std::vector<VkImageView> views(count);

			for (uint32_t i = 0; i < count; i++)
			{
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

			VK_CHECK_RESULT(vkCreateFramebuffer(vktDevice->vk(), &framebufferInfo, nullptr, &frameBuffer));

			pDeletionQueue->push_function([=]() { vkDestroyFramebuffer(vktDevice->vk(), frameBuffer, nullptr); });

			return frameBuffer;
		}

		class RenderPassBuilder : IBuilder<VkRenderPass>
		{
		  public:
			RenderPassBuilder(const RenderPassBuilder&) = delete; // no copy, force passing references or pointers

			RenderPassBuilder(Logical::Device* vktDevice) : vktDevice(vktDevice)
			{
			}

			RenderPassBuilder& addAttachment(VkAttachmentDescription attachDesc, int tag);

			/**
			Automatically converts src/dst subpass tags to internal indices
			*/
			RenderPassBuilder& addSubpassDependency(VkSubpassDependency dependency);

			class SubpassBuilder : IBuilder<VkSubpassDescription>
			{
			  public:
				SubpassBuilder& addColorAttachmentRef(int attachmentTag, VkImageLayout layout);
				SubpassBuilder& preserveAttachments(std::vector<int>& attachmentTags);
				SubpassBuilder& setDepthStencilRef(int attachmentTag);
				RenderPassBuilder& endSubpass();

				friend class RenderPassBuilder;

				SubpassBuilder(const SubpassBuilder& s) = delete; // force return by reference
			  private:
				~SubpassBuilder();

				SubpassBuilder(RenderPassBuilder* caller, std::vector<int>& attachmentTags,
							   VkPipelineBindPoint bindPoint);
				VkSubpassDescription build() override;

				RenderPassBuilder* caller;
				VkSubpassDescription subpass;
				std::vector<int> renderPassAttachmentTags;

				std::vector<VkAttachmentReference> colorRefs{};
				std::vector<uint32_t> preserveRefs{};
				VkAttachmentReference* depthRef = nullptr;
			};

			SubpassBuilder& initSubpass(VkPipelineBindPoint bindPoint, int tag);

			VkRenderPass build() override;

		  private:
			Logical::Device* vktDevice;

			std::vector<VkSubpassDependency> dependencies{};

			std::vector<int> attachmentTags{};
			std::vector<VkAttachmentDescription> attachments{};

			std::vector<int> subpassTags{};
			std::vector<SubpassBuilder*> subpassBuilders{};
		};

		inline VkShaderModule createShaderModule(Logical::Device* vktDevice, std::string spvPath)
		{
			const std::vector<char> code = io::readFile(spvPath);

			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = code.size();
			createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

			VkShaderModule shaderModule;
			if (vkCreateShaderModule(vktDevice->vk(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create shader module!");
			}

			return shaderModule;
		}
	}; // namespace pipeline

} // namespace vkt