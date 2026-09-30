/**
 * @file
 * @brief The shader contract: GLSL to SPIR-V, Params reflection, //@param, the stored JSON form.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "render/shader_compiler.h"

#include <nlohmann/json.hpp>
#include <shaderc/shaderc.hpp>
#include <spirv_reflect.h>

#include <cstring>
#include <format>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ReaShader::gpu
{
	namespace
	{
		// Prepended to every user shader: the shader contract.
		// Keep ReaShaderInputs in sync with ShaderInputs in shader_compiler.h.
		constexpr const char* kShaderPreamble = R"(
layout(location = 0) in vec2 uv;                          // 0..1 over the frame, (0, 0) = top left
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D iChannel0; // the input video frame
layout(set = 0, binding = 2) uniform sampler3D iChannel1; // the shader node's LUT (an identity when it has none)

layout(push_constant) uniform ReaShaderInputs
{
    vec2 iResolution; // frame size in pixels
    float iTime;      // project time in seconds
    float iFrameRate;
    int iFrame;       // frames rendered since the shader was loaded
};

// iChannel1 applied to a color: 0 and 1 land on the LUT's first and last entries (texel centers)
vec3 iLut(vec3 color)
{
    float size = float(textureSize(iChannel1, 0).x);
    return texture(iChannel1, (clamp(color, 0.0, 1.0) * (size - 1.0) + 0.5) / size).rgb;
}
)";

		std::vector<uint32_t> compileGlsl(const std::string& source, shaderc_shader_kind kind, const std::string& name)
		{
			shaderc::CompileOptions options;
			options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
			options.SetAutoBindUniforms(true);
			options.SetBindingBase(shaderc_uniform_kind_buffer, kParamsBinding);
			options.SetBindingBase(shaderc_uniform_kind_texture, kLutBinding + 1);
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
				// a block without an instance name (`uniform Params { ... };`) is named by its type
				std::string name = binding->name;
				if (name.empty() && binding->type_description && binding->type_description->type_name)
					name = binding->type_description->type_name;

				// the pipeline layout has one descriptor set: anything outside it is invalid for Vulkan
				if (binding->set != 0)
				{
					error = std::format("'{}': only descriptor set 0 is available", name);
					break;
				}
				// the preamble's samplers (a user declaration at their bindings would alias them)
				if ((binding->binding == kInputBinding && name == "iChannel0") ||
					(binding->binding == kLutBinding && name == "iChannel1"))
					continue;

				if (binding->binding != kParamsBinding ||
					binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
				{
					error = std::format("'{}': only iChannel0, iChannel1 and one uniform block (Params) are available", name);
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

	} // namespace

	CompiledShader compileShader(const std::string& source, const std::string& name)
	{
		CompiledShader shader;
		shader.spirv = compileGlsl(withPreamble(source), shaderc_fragment_shader, name);
		reflect(shader, parseAnnotations(source));
		return shader;
	}

	// -------- stored form --------

	constexpr int kStoredVersion = 1;

	nlohmann::json toJson(const CompiledShader& shader)
	{
		nlohmann::json params = nlohmann::json::array();
		for (const ShaderParamField& field : shader.params)
		{
			params.push_back({ { "name", field.name },
							   { "label", field.label },
							   { "defaultValue", field.defaultValue },
							   { "minValue", field.minValue },
							   { "maxValue", field.maxValue },
							   { "offset", field.offset } });
		}
		return { { "version", kStoredVersion },
				 { "paramsSize", shader.paramsSize },
				 { "params", params },
				 { "spirv", shader.spirv } };
	}

	CompiledShader fromJson(const nlohmann::json& stored)
	{
		if (stored.value("version", 0) != kStoredVersion)
			throw std::runtime_error("Unsupported compiled shader version");

		CompiledShader shader;
		shader.paramsSize = stored.at("paramsSize").get<uint32_t>();
		shader.spirv = stored.at("spirv").get<std::vector<uint32_t>>();
		for (const nlohmann::json& param : stored.at("params"))
		{
			ShaderParamField field;
			field.name = param.at("name").get<std::string>();
			field.label = param.at("label").get<std::string>();
			field.defaultValue = param.at("defaultValue").get<float>();
			field.minValue = param.at("minValue").get<float>();
			field.maxValue = param.at("maxValue").get<float>();
			field.offset = param.at("offset").get<uint32_t>();
			shader.params.push_back(field);
		}
		if (shader.spirv.empty())
			throw std::runtime_error("Compiled shader has no code");
		return shader;
	}
} // namespace ReaShader::gpu
