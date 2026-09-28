/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "tools/logging.h"

#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include "glslang/SPIRV/GlslangToSpv.h"
#include "glslang/SPIRV/SpvTools.h"

#include "vktdevices.h"
#include "vktimages.h"
#include "vktrendering.h"

#include <spirv_cross.hpp>

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

		class MaterialBuilder : IBuilder<vkt::Rendering::Material>
		{
		  public:
			MaterialBuilder(const MaterialBuilder&) = delete; // no copy, force passing references or pointers

			MaterialBuilder(Logical::Device* vktDevice) : vktDevice(vktDevice)
			{
				//////////////////////////
				//		pipeline cache
				//////////////////////////

				pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				VK_CHECK_RESULT(vkCreatePipelineCache(vktDevice->vk(), &pipelineCacheCreateInfo, nullptr,
													  &(material.pipelineCache)));
			}
			~MaterialBuilder()
			{
				delete pipelineBuilder;
				delete pipelineLayoutbuilder;
			}

			class PipelineLayoutBuilder
			{
			  public:
				friend class MaterialBuilder;

				PipelineLayoutBuilder(const PipelineLayoutBuilder& s) = delete; // force return by reference

				PipelineLayoutBuilder& setPushConstants(VkPushConstantRange& pPushConstantRanges)
				{
					// push constants
					pipelineLayoutInfo.pushConstantRangeCount = 1;
					pipelineLayoutInfo.pPushConstantRanges = &pPushConstantRanges;

					return *this;
				}
				PipelineLayoutBuilder& setDescriptors(std::vector<VkDescriptorSetLayout>& descriptorSetLayouts)
				{
					// descriptors
					pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
					pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

					return *this;
				}

				MaterialBuilder& endPipelineLayout()
				{
					VK_CHECK_RESULT(vkCreatePipelineLayout(caller->vktDevice->vk(), &pipelineLayoutInfo, nullptr,
														   &(caller->material.pipelineLayout)));
					return *caller;
				}

			  private:
				PipelineLayoutBuilder(MaterialBuilder* caller) : caller(caller)
				{
					///////////////////////////
					//		pipeline layout
					//////////////////////////
					pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				};

				MaterialBuilder* caller;

				VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			};

			friend class PipelineLayoutBuilder;

			PipelineLayoutBuilder& beginPipelineLayout()
			{
				pipelineLayoutbuilder = new PipelineLayoutBuilder(this);
				return *pipelineLayoutbuilder;
			}

			class PipelineBuilder
			{
			  public:
				friend class MaterialBuilder;

				PipelineBuilder(const PipelineLayoutBuilder& s) = delete; // force return by reference

				PipelineBuilder& setShaderStages(std::vector<VkPipelineShaderStageCreateInfo>&& shaderStages)
				{
					// shader stages
					pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
					;
					pipelineInfo.pStages = shaderStages.data();

					return *this;
				}

				PipelineBuilder& setDynamicStates(std::vector<VkDynamicState>&& dynamicStates)
				{
					dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
					dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
					dynamicState.pDynamicStates = dynamicStates.data();

					pipelineInfo.pDynamicState = &dynamicState;

					return *this;
				}
				PipelineBuilder& setVertexState(VkPipelineVertexInputStateCreateInfo& vertexInputInfo)
				{
					// vertex state
					pipelineInfo.pVertexInputState = &vertexInputInfo;
					return *this;
				}
				PipelineBuilder& setInputAssembly(VkPipelineInputAssemblyStateCreateInfo& inputAssembly)
				{
					pipelineInfo.pInputAssemblyState = &inputAssembly;

					return *this;
				}
				PipelineBuilder& setViewPortState(VkPipelineViewportStateCreateInfo& viewportState)
				{
					pipelineInfo.pViewportState = &viewportState;
					return *this;
				}
				PipelineBuilder& setRasterizer(VkPipelineRasterizationStateCreateInfo& rasterizer)
				{
					pipelineInfo.pRasterizationState = &rasterizer;
					return *this;
				}
				PipelineBuilder& setMultisampling(VkPipelineMultisampleStateCreateInfo& multisampling)
				{
					pipelineInfo.pMultisampleState = &multisampling;
					return *this;
				}
				PipelineBuilder& setDepthStencil(VkPipelineDepthStencilStateCreateInfo& depthStencilInfo)
				{
					pipelineInfo.pDepthStencilState = &depthStencilInfo;
					return *this;
				}
				PipelineBuilder& setColorBlending(VkPipelineColorBlendStateCreateInfo& colorBlending)
				{
					pipelineInfo.pColorBlendState = &colorBlending;

					return *this;
				}

				MaterialBuilder& endPipeline(VkRenderPass renderPass)
				{
					vkt::Rendering::Material& material = caller->material;
					vkt::Logical::Device* vktDevice = caller->vktDevice;

					// layout
					pipelineInfo.layout = material.pipelineLayout;
					pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
					pipelineInfo.basePipelineIndex = -1;			  // Optional
					// renderpass
					pipelineInfo.renderPass = renderPass;
					// pipelineInfo.subpass = 0;

					VK_CHECK_RESULT(vkCreateGraphicsPipelines(vktDevice->vk(), material.pipelineCache, 1, &pipelineInfo,
															  nullptr, &(material.pipeline)));

					vktDevice->pDeletionQueue->push_function([=]() {
						vkDestroyPipeline(vktDevice->vk(), material.pipeline, nullptr);
						vkDestroyPipelineCache(vktDevice->vk(), material.pipelineCache, nullptr);
						vkDestroyPipelineLayout(vktDevice->vk(), material.pipelineLayout, nullptr);
					});

					return *caller;
				}

			  private:
				PipelineBuilder(MaterialBuilder* caller) : caller(caller)
				{
					//////////////////////////
					//		pipeline
					//////////////////////////

					pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				};

				MaterialBuilder* caller;

				VkPipelineDynamicStateCreateInfo dynamicState{};
				VkGraphicsPipelineCreateInfo pipelineInfo{};
			};

			friend class PipelineBuilder;

			PipelineBuilder& beginPipeline()
			{
				pipelineBuilder = new PipelineBuilder(this);
				return *pipelineBuilder;
			}

			vkt::Rendering::Material build()
			{
				return material;
			};

		  private:
			Logical::Device* vktDevice;

			vkt::Rendering::Material material{};

			PipelineLayoutBuilder* pipelineLayoutbuilder;
			PipelineBuilder* pipelineBuilder;

			VkPipelineCacheCreateInfo pipelineCacheCreateInfo{};
		};

		namespace Shader
		{
			inline bool compile_glsl_to_spirv(std::string& glslSource, EShLanguage stage,
											  std::vector<uint32_t>& spirvCodeOut, std::string& msgOut)
			{
				glslang::InitializeProcess(); // Initialize glslang

				// Create a shader object
				glslang::TShader shader(stage);
				const char* shaderStrings[1] = { glslSource.data() };
				shader.setStrings(shaderStrings, 1);

				// Set up shader compilation options
				int defaultVersion = 100;		 // overridden by #version in the shader
				EProfile profile = ECoreProfile; // Use core profile
				const char* entryPoint = "main"; // Entry point function name

				shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, defaultVersion);
				shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
				shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

				// Enable/disable options as needed
				// shader.setAutoMapBindings(true);
				// shader.setAutoMapLocations(true);

				// Compile GLSL to SPIR-V
				if (!shader.parse(GetDefaultResources(), defaultVersion, false, EShMsgDefault))
				{
					msgOut = shader.getInfoLog();
					glslang::FinalizeProcess();
					return false;
				}

				glslang::TProgram program;
				program.addShader(&shader);

				if (!program.link(EShMsgDefault))
				{
					msgOut = program.getInfoLog();
					glslang::FinalizeProcess();
					return false;
				}

				// Get the intermediate representation
				const glslang::TIntermediate* intermediate = program.getIntermediate(stage);

				// Convert GLSL to SPIR-V
				glslang::GlslangToSpv(*intermediate, spirvCodeOut);

				msgOut = "Compiled Succesfully";

				glslang::FinalizeProcess(); // Clean up glslang
				return true;
			}
			inline bool compile_glsl_to_spirv(std::vector<char>&& glslData, EShLanguage stage,
											  std::vector<uint32_t>& spirvCodeOut, std::string& msgOut)
			{
				// convert to string
				glslData.push_back('\0');
				std::string vertSource(std::move(glslData.data()));
				// compile
				return compile_glsl_to_spirv(vertSource, stage, spirvCodeOut, msgOut);
			}
			
			struct Variable
			{
				Variable() = default;

				std::string name;
				spirv_cross::SPIRType::BaseType baseType;

				Variable(spirv_cross::Compiler& c, spirv_cross::Resource& res)
					: name(res.name), baseType(c.get_type(res.base_type_id).basetype)
				{
				}
			};

			struct Member : Variable
			{
				uint32_t rows, cols;

				Member(spirv_cross::Compiler& c, const spirv_cross::SPIRType& parent, uint32_t memberIndex)
				{
					uint32_t memberId = parent.member_types[memberIndex];
					const auto& memberType = c.get_type(memberId);

					name = c.get_member_name(parent.self, memberIndex);
					baseType = memberType.basetype;

					rows = memberType.vecsize, cols = memberType.columns;
				}

				static void extractMembers(std::vector<std::unique_ptr<Member>>& dst, spirv_cross::Compiler& c, const spirv_cross::SPIRType& parent)
				{
					for (uint32_t i = 0; i < parent.member_types.size(); ++i)
					{
						std::unique_ptr<Member> m = std::make_unique<Member>(c, parent, i);

						dst.push_back(std::move(m));
					}
				}
			};

			struct Struct : Variable
			{
				std::vector<std::unique_ptr<Member>> members;

				Struct(spirv_cross::Compiler& c, spirv_cross::Resource& res) : Variable(c, res)
				{
					const auto& type = c.get_type(res.base_type_id);
					Member::extractMembers(members, c, type);
				}
			};

			struct BoundSet
			{
				uint32_t set;
				uint32_t binding;

				BoundSet(spirv_cross::Compiler& c, spirv_cross::Resource& res)
				{
					binding = c.get_decoration(res.id, spv::DecorationBinding);
					set = c.get_decoration(res.id, spv::DecorationDescriptorSet);
				}
			};

			struct UniformBuffer : Struct, BoundSet
			{
				uint32_t size;

				UniformBuffer(spirv_cross::Compiler& c, spirv_cross::Resource& res) : Struct(c, res), BoundSet(c, res)
				{
					const auto& type = c.get_type(res.base_type_id);

					size = c.get_declared_struct_size(type);
				}
			};

			struct PushConstants : Struct
			{
				PushConstants(spirv_cross::Compiler& c, spirv_cross::Resource& res) : Struct(c,res)
				{
					
				}
			};

			struct SampledImage : Variable, BoundSet
			{
				SampledImage(spirv_cross::Compiler& c, spirv_cross::Resource& res)
					: Variable(c, res), BoundSet(c, res)
				{

				}
			};
		}

		using namespace Shader;
		
		inline VkShaderModule createShaderModule(Logical::Device* vktDevice, const uint32_t* spvCode, size_t size)
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
		inline VkShaderModule createShaderModule(Logical::Device* vktDevice, std::vector<uint32_t>& spvCode)
		{
			return createShaderModule(vktDevice, spvCode.data(), spvCode.size() * sizeof(uint32_t));
		}
		inline VkShaderModule createShaderModule(Logical::Device* vktDevice, std::string spvPath)
		{
			const std::vector<char> code = io::readFile(spvPath);
			return createShaderModule(vktDevice, reinterpret_cast<const uint32_t*>(code.data()), code.size());
		}
		inline VkShaderModule createShaderModule(Logical::Device* vktDevice, EShLanguage stage,
												 std::vector<char>&& glslData, std::string& msgOut)
		{
			std::vector<uint32_t> vertSpv;
			compile_glsl_to_spirv(std::move(glslData), stage, vertSpv, msgOut);
			// generate
			return vkt::Pipeline::createShaderModule(vktDevice, vertSpv);
		}
		inline VkShaderModule createShaderModule(Logical::Device* vktDevice, EShLanguage stage, std::string glslPath,
												 std::string& msgOut)
		{
			std::vector<char> glslData = vkt::io::readFile(glslPath);
			return vkt::Pipeline::createShaderModule(vktDevice, stage, std::move(glslData), msgOut);
		}

	}; // namespace Pipeline

} // namespace vkt
