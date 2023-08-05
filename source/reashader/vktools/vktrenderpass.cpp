#include "vktpipeline.h"

namespace vkt
{
	namespace Pipeline
	{
		using SubpassBuilder = RenderPassBuilder::SubpassBuilder;

		// RenderPassBuilder

		RenderPassBuilder& RenderPassBuilder::addAttachment(VkAttachmentDescription attachDesc, int tag)
		{
			attachments.push_back(std::move(attachDesc));
			attachmentTags.push_back(tag);
			return *this;
		}

		RenderPassBuilder& RenderPassBuilder::addSubpassDependency(VkSubpassDependency dependency)
		{

			dependency.srcSubpass = dependency.srcSubpass == VK_SUBPASS_EXTERNAL
										? VK_SUBPASS_EXTERNAL
										: (uint32_t)vectors::findPos(subpassTags, (int)dependency.srcSubpass);
			dependency.dstSubpass = dependency.dstSubpass == VK_SUBPASS_EXTERNAL
										? VK_SUBPASS_EXTERNAL
										: (uint32_t)vectors::findPos(subpassTags, (int)dependency.dstSubpass);

			dependencies.push_back(dependency);

			return *this;
		}

		SubpassBuilder& RenderPassBuilder::initSubpass(VkPipelineBindPoint bindPoint, int tag)
		{
			subpassBuilders.push_back(new SubpassBuilder(this, attachmentTags, bindPoint));
			subpassTags.push_back(tag);
			return *(subpassBuilders.back());
		}

		VkRenderPass RenderPassBuilder::build()
		{

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassInfo.pAttachments = attachments.data();

			uint32_t subpassCount = static_cast<uint32_t>(subpassBuilders.size());
			std::vector<VkSubpassDescription> subpasses(subpassCount);
			for (uint32_t i = 0; i < subpassCount; i++)
			{
				subpasses[i] = std::move(subpassBuilders[i]->build());
			}

			renderPassInfo.subpassCount = subpassCount;
			renderPassInfo.pSubpasses = subpasses.data();

			renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
			renderPassInfo.pDependencies = dependencies.data();

			VkRenderPass renderPass;
			VK_CHECK_RESULT(vkCreateRenderPass(vktDevice->vk(), &renderPassInfo, nullptr, &renderPass));

			for (auto subpass : subpassBuilders)
			{
				delete subpass;
			}

			VkDevice device = vktDevice->vk();
			vktDevice->pDeletionQueue->push_function([=]() { vkDestroyRenderPass(device, renderPass, nullptr); });

			return renderPass;
		}

		// ---------

		SubpassBuilder::SubpassBuilder(RenderPassBuilder* caller, std::vector<int>& attachmentTags,
									   VkPipelineBindPoint bindPoint)
			: caller(caller), renderPassAttachmentTags(attachmentTags)
		{
			subpass = {};
			subpass.pipelineBindPoint = bindPoint;
		}

		SubpassBuilder::~SubpassBuilder()
		{
			if (depthRef)
				delete (depthRef);
		}

		SubpassBuilder& SubpassBuilder::addColorAttachmentRef(int attachmentTag, VkImageLayout layout)
		{
			colorRefs.push_back({ (uint32_t)vectors::findPos(renderPassAttachmentTags, attachmentTag), layout });
			return *this;
		}
		SubpassBuilder& SubpassBuilder::preserveAttachments(std::vector<int>& attachmentTags)
		{
			for (const auto& tag : attachmentTags)
			{
				preserveRefs.push_back((uint32_t)vectors::findPos(renderPassAttachmentTags, tag));
			}
			return *this;
		}
		SubpassBuilder& SubpassBuilder::setDepthStencilRef(int attachmentTag)
		{

			if (depthRef)
				delete (depthRef);

			depthRef = new VkAttachmentReference();
			depthRef->attachment = (uint32_t)vectors::findPos(renderPassAttachmentTags, attachmentTag);
			depthRef->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			subpass.pDepthStencilAttachment = depthRef;

			return *this;
		}
		RenderPassBuilder& SubpassBuilder::endSubpass()
		{
			subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
			subpass.pColorAttachments = colorRefs.data();
			subpass.preserveAttachmentCount = static_cast<uint32_t>(preserveRefs.size());
			subpass.pPreserveAttachments = preserveRefs.data();
			return *caller;
		}
		VkSubpassDescription SubpassBuilder::build()
		{
			return subpass;
		}

	} // namespace Pipeline
} // namespace vkt
