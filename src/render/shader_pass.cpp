/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "render/shader_pass.h"

#include <shaderc/shaderc.hpp>
#include <spirv_reflect.h>

#include <cstring>
#include <format>
#include <map>
#include <sstream>

namespace ReaShader::gpu
{
	namespace
	{
		// Prepended to every user shader: the shader contract.
		// Keep ReaShaderInputs in sync with ShaderInputs in shader_pass.h.
		constexpr const char* kShaderPreamble = R"(
layout(location = 0) in vec2 uv;                          // 0..1 over the frame, (0, 0) = top left
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D iChannel0; // the input video frame

layout(push_constant) uniform ReaShaderInputs
{
    vec2 iResolution; // frame size in pixels
    float iTime;      // project time in seconds
    float iFrameRate;
    int iFrame;       // frames rendered since the shader was loaded
    float videoParam; // the plugin's Video Param, in [0, 1]
};
)";

		// Covers the frame with one triangle, no vertex buffer needed
		constexpr const char* kFullscreenVertexShader = R"(#version 450
layout(location = 0) out vec2 uv;
void main()
{
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

		// Params block: binding 1 (auto-assigned when the shader doesn't say)
		constexpr uint32_t kInputBinding = 0;
		constexpr uint32_t kParamsBinding = 1;

		std::vector<uint32_t> compileGlsl(const std::string& source, shaderc_shader_kind kind, const std::string& name)
		{
			shaderc::CompileOptions options;
			options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
			options.SetAutoBindUniforms(true);
			options.SetBindingBase(shaderc_uniform_kind_buffer, kParamsBinding);
			options.SetBindingBase(shaderc_uniform_kind_texture, kParamsBinding + 1);
			options.SetAutoMapLocations(true);

			shaderc::Compiler compiler;
			shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(source, kind, name.c_str(), options);
			if (result.GetCompilationStatus() != shaderc_compilation_status_success)
				throw std::runtime_error(result.GetErrorMessage());
			return { result.cbegin(), result.cend() };
		}

		// "#version 450 + preamble + #line 1 + user source", with the user's #version dropped and their
		// #extension lines moved above the preamble (they must precede declarations). Line count is kept.
		std::string withPreamble(const std::string& source)
		{
			std::string extensions;
			std::string body;

			std::istringstream lines(source);
			std::string line;
			while (std::getline(lines, line))
			{
				size_t start = line.find_first_not_of(" \t");
				std::string_view directive = start == std::string::npos ? "" : std::string_view(line).substr(start);
				if (directive.starts_with("#extension"))
					extensions += line + "\n";
				if (directive.starts_with("#version") || directive.starts_with("#extension"))
					line.clear();
				body += line + "\n";
			}

			return "#version 450\n" + extensions + kShaderPreamble + "#line 1\n" + body;
		}

		// `//@param member 'Label' default min max`: label and numbers are optional, in that order
		struct Annotation
		{
			std::string label;
			float defaultValue = 0.5f;
			float minValue = 0.0f;
			float maxValue = 1.0f;
		};

		std::map<std::string, Annotation> parseAnnotations(const std::string& source)
		{
			std::map<std::string, Annotation> annotations;

			std::istringstream lines(source);
			std::string line;
			while (std::getline(lines, line))
			{
				size_t start = line.find("//@param");
				if (start == std::string::npos)
					continue;

				std::istringstream tokens(line.substr(start + std::strlen("//@param")));
				std::string member;
				if (!(tokens >> member))
					continue;

				Annotation annotation;
				annotation.label = member;
				tokens >> std::ws;
				constexpr char quote = '\'';
				if (tokens.peek() == quote)
				{
					tokens.get();
					std::getline(tokens, annotation.label, quote);
				}
				float number;
				if (tokens >> number)
				{
					annotation.defaultValue = number;
					if (tokens >> number)
					{
						annotation.minValue = number;
						if (tokens >> number)
							annotation.maxValue = number;
					}
				}
				annotations[member] = annotation;
			}
			return annotations;
		}

		// Finds the Params block: every float (or vector component) in it becomes a slider
		void reflect(CompiledShader& shader, const std::map<std::string, Annotation>& annotations)
		{
			SpvReflectShaderModule module{};
			if (spvReflectCreateShaderModule(shader.spirv.size() * sizeof(uint32_t), shader.spirv.data(), &module) !=
				SPV_REFLECT_RESULT_SUCCESS)
				throw std::runtime_error("Shader reflection failed");

			uint32_t count = 0;
			spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
			std::vector<SpvReflectDescriptorBinding*> bindings(count);
			spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

			std::string error;
			for (const SpvReflectDescriptorBinding* binding : bindings)
			{
				if (binding->binding == kInputBinding)
					continue;

				if (binding->binding != kParamsBinding ||
					binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
				{
					error = std::format("'{}': only iChannel0 and one uniform block (Params) are available", binding->name);
					break;
				}

				const SpvReflectBlockVariable& block = binding->block;
				shader.paramsSize = block.padded_size;
				for (uint32_t i = 0; i < block.member_count && error.empty(); i++)
				{
					const SpvReflectBlockVariable& member = block.members[i];
					SpvReflectTypeFlags type = member.type_description->type_flags;
					bool isFloatOrVector = (type & SPV_REFLECT_TYPE_FLAG_FLOAT) &&
										   !(type & (SPV_REFLECT_TYPE_FLAG_MATRIX | SPV_REFLECT_TYPE_FLAG_ARRAY));
					if (!isFloatOrVector)
					{
						error = std::format("Params member '{}': only float, vec2, vec3 and vec4 are supported", member.name);
						break;
					}

					auto found = annotations.find(member.name);
					Annotation annotation = found != annotations.end() ? found->second : Annotation{ member.name };

					uint32_t components = (type & SPV_REFLECT_TYPE_FLAG_VECTOR) ? member.numeric.vector.component_count : 1;
					for (uint32_t c = 0; c < components; c++)
					{
						bool vector = components > 1;
						ShaderParamField field;
						field.name = vector ? std::format("{}.{}", member.name, "xyzw"[c]) : member.name;
						field.label = vector ? std::format("{}.{}", annotation.label, "xyzw"[c]) : annotation.label;
						field.defaultValue = annotation.defaultValue;
						field.minValue = annotation.minValue;
						field.maxValue = annotation.maxValue;
						field.offset = member.offset + c * (uint32_t)sizeof(float);
						shader.params.push_back(field);
					}
				}
			}

			spvReflectDestroyShaderModule(&module);
			if (!error.empty())
				throw std::runtime_error(error);
		}

		VkShaderModule createShaderModule(VkDevice device, const std::vector<uint32_t>& spirv)
		{
			VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
			moduleInfo.pCode = spirv.data();
			VkShaderModule module = VK_NULL_HANDLE;
			VK_CHECK(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));
			return module;
		}
	} // namespace

	CompiledShader compileShader(const std::string& source, const std::string& name)
	{
		CompiledShader shader;
		shader.spirv = compileGlsl(withPreamble(source), shaderc_fragment_shader, name);
		reflect(shader, parseAnnotations(source));
		return shader;
	}

	// -------- ShaderPass --------

	void ShaderPass::create(Context& context, const CompiledShader& shader)
	{
		VkDevice device = context.device;
		params = shader.params;

		// descriptors: binding 0 = iChannel0, binding 1 = Params (always present, even if the shader has none)
		VkDescriptorSetLayoutBinding bindings[] = {
			{ kInputBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ kParamsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = 2;
		setLayoutInfo.pBindings = bindings;
		VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout));

		VkPushConstantRange pushConstants{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderInputs) };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &setLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstants;
		VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		setInfo.descriptorPool = context.descriptorPool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &setLayout;
		VK_CHECK(vkAllocateDescriptorSets(device, &setInfo, &descriptorSet));

		// Params values, rewritten every frame
		paramsBuffer.create(context.allocator, shader.paramsSize > 16 ? shader.paramsSize : 16,
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		std::memset(paramsBuffer.mapped, 0, (size_t)paramsBuffer.size);

		VkDescriptorBufferInfo bufferInfo{ paramsBuffer.buffer, 0, VK_WHOLE_SIZE };
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = descriptorSet;
		write.dstBinding = kParamsBinding;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo = &bufferInfo;
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		// pipeline: fullscreen triangle, no blending, renders straight to the frame format
		VkShaderModule vertexModule =
			createShaderModule(device, compileGlsl(kFullscreenVertexShader, shaderc_vertex_shader, "fullscreen.vert"));
		VkShaderModule fragmentModule = createShaderModule(device, shader.spirv);

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertexModule;
		stages[0].pName = "main";
		stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragmentModule;
		stages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{
			VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
		};
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
		};
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_NONE;
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blendAttachment{};
		blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
										 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAttachment;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamic.dynamicStateCount = 2;
		dynamic.pDynamicStates = dynamicStates;

		VkFormat colorFormat = kFrameFormat;
		VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachmentFormats = &colorFormat;

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &rendering;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pColorBlendState = &blend;
		pipelineInfo.pDynamicState = &dynamic;
		pipelineInfo.layout = pipelineLayout;

		VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
		vkDestroyShaderModule(device, fragmentModule, nullptr);
		vkDestroyShaderModule(device, vertexModule, nullptr);
		VK_CHECK(result);
	}

	void ShaderPass::destroy(Context& context)
	{
		VkDevice device = context.device;
		if (pipeline)
			vkDestroyPipeline(device, pipeline, nullptr);
		if (pipelineLayout)
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		if (descriptorSet)
			vkFreeDescriptorSets(device, context.descriptorPool, 1, &descriptorSet);
		if (setLayout)
			vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		paramsBuffer.destroy(context.allocator);
		*this = {};
	}

	void ShaderPass::bindInput(Context& context, VkImageView input)
	{
		VkDescriptorImageInfo imageInfo{ context.sampler, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = descriptorSet;
		write.dstBinding = kInputBinding;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	void ShaderPass::writeParams(Context& context, const float* values, size_t count)
	{
		auto* block = static_cast<uint8_t*>(paramsBuffer.mapped);
		for (size_t i = 0; i < params.size(); i++)
		{
			float value = i < count ? values[i] : params[i].defaultValue;
			std::memcpy(block + params[i].offset, &value, sizeof(float));
		}
		paramsBuffer.flush(context.allocator);
	}

	void ShaderPass::record(VkCommandBuffer commandBuffer, const Image& target, const ShaderInputs& inputs)
	{
		transition(commandBuffer, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

		VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = target.view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // every pixel gets written
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.renderArea = { { 0, 0 }, target.extent };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		VkViewport viewport{ 0, 0, (float)target.extent.width, (float)target.extent.height, 0, 1 };
		VkRect2D scissor{ { 0, 0 }, target.extent };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet,
								0, nullptr);
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderInputs),
						   &inputs);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);

		vkCmdEndRendering(commandBuffer);
	}
} // namespace ReaShader::gpu
