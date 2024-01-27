/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "tools/logging.h"


#include "glslang/Public/ShaderLang.h"
#include "glslang/Public/ResourceLimits.h"
#include "glslang/SPIRV/SpvTools.h"
#include "glslang/SPIRV/GlslangToSpv.h"

#include "vktdevices.h"
#include "vktimages.h"

namespace vkt
{
namespace Pipeline
{

inline VkFramebuffer createFramebuffer(Logical::Device *vktDevice, deletion_queue *pDeletionQueue,
                                       VkRenderPass renderPass, VkExtent2D extent,
                                       std::vector<Images::AllocatedImage *> attachments)
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
    RenderPassBuilder(const RenderPassBuilder &) = delete; // no copy, force passing references or pointers

    RenderPassBuilder(Logical::Device *vktDevice) : vktDevice(vktDevice)
    {
    }

    RenderPassBuilder &addAttachment(VkAttachmentDescription attachDesc, int tag);

    /**
    Automatically converts src/dst subpass tags to internal indices
    */
    RenderPassBuilder &addSubpassDependency(VkSubpassDependency dependency);

    class SubpassBuilder : IBuilder<VkSubpassDescription>
    {
      public:
        SubpassBuilder &addColorAttachmentRef(int attachmentTag, VkImageLayout layout);
        SubpassBuilder &preserveAttachments(std::vector<int> &attachmentTags);
        SubpassBuilder &setDepthStencilRef(int attachmentTag);
        RenderPassBuilder &endSubpass();

        friend class RenderPassBuilder;

        SubpassBuilder(const SubpassBuilder &s) = delete; // force return by reference
      private:
        ~SubpassBuilder();

        SubpassBuilder(RenderPassBuilder *caller, std::vector<int> &attachmentTags, VkPipelineBindPoint bindPoint);
        VkSubpassDescription build() override;

        RenderPassBuilder *caller;
        VkSubpassDescription subpass;
        std::vector<int> renderPassAttachmentTags;

        std::vector<VkAttachmentReference> colorRefs{};
        std::vector<uint32_t> preserveRefs{};
        VkAttachmentReference *depthRef = nullptr;
    };

    SubpassBuilder &initSubpass(VkPipelineBindPoint bindPoint, int tag);

    VkRenderPass build() override;

  private:
    Logical::Device *vktDevice;

    std::vector<VkSubpassDependency> dependencies{};

    std::vector<int> attachmentTags{};
    std::vector<VkAttachmentDescription> attachments{};

    std::vector<int> subpassTags{};
    std::vector<SubpassBuilder *> subpassBuilders{};
};

inline bool compile_glsl_to_spirv(std::string &glslSource, EShLanguage stage, std::vector<uint32_t> &spirvCode)
{
    glslang::InitializeProcess(); // Initialize glslang

    // Create a shader object
    glslang::TShader shader(stage);
    const char *shaderStrings[1] = {glslSource.data()};
    shader.setStrings(shaderStrings, 1);

    // Set up shader compilation options
    int defaultVersion = 100;        // overridden by #version in the shader
    EProfile profile = ECoreProfile; // Use core profile
    const char *entryPoint = "main"; // Entry point function name

    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, defaultVersion);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

    // Enable/disable options as needed
    // shader.setAutoMapBindings(true);
    // shader.setAutoMapLocations(true);

    // Compile GLSL to SPIR-V
    if (!shader.parse(GetDefaultResources(), defaultVersion, false, EShMsgDefault))
    {
        LOG(WARNING, toFile | toConsole | toBox, "ReaShaderRenderer", "GLSL Compilation Failed", shader.getInfoLog());
        glslang::FinalizeProcess();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(EShMsgDefault))
    {
        LOG(WARNING, toFile | toConsole | toBox, "ReaShaderRenderer", "GLSL Linking Failed", program.getInfoLog());
        glslang::FinalizeProcess();
        return false;
    }

    // Get the intermediate representation
    const glslang::TIntermediate *intermediate = program.getIntermediate(stage);

    // Convert GLSL to SPIR-V
    glslang::GlslangToSpv(*intermediate, spirvCode);

    glslang::FinalizeProcess(); // Clean up glslang
    return true;
}

inline VkShaderModule createShaderModule(Logical::Device *vktDevice, const uint32_t *spvCode, size_t size)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = spvCode;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vktDevice->vk(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}
inline VkShaderModule createShaderModule(Logical::Device *vktDevice, std::vector<uint32_t> &spvCode)
{
    return createShaderModule(vktDevice, spvCode.data(), spvCode.size() * sizeof(uint32_t));
}
inline VkShaderModule createShaderModule(Logical::Device *vktDevice, std::string spvPath)
{
    const std::vector<char> code = io::readFile(spvPath);
    return createShaderModule(vktDevice, reinterpret_cast<const uint32_t *>(code.data()), code.size());
}
inline VkShaderModule createShaderModule(Logical::Device *vktDevice, EShLanguage stage, std::string glslPath)
{
    std::vector<char> vertRaw = vkt::io::readFile(glslPath);
	vertRaw.push_back('\0');
	std::string vertSource(vertRaw.data());
    std::vector<uint32_t> vertSpv;
    compile_glsl_to_spirv(vertSource, stage, vertSpv);
    return vkt::Pipeline::createShaderModule(vktDevice, vertSpv);
}

}; // namespace Pipeline

} // namespace vkt
