/**
 * @file
 * @brief User shaders: GLSL written against the shader contract (kShaderPreamble), compiled to SPIR-V once, on upload, and stored as JSON.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ReaShader::gpu
{
	// Descriptor bindings of every shader
	constexpr uint32_t kInputBinding = 0;  // iChannel0
	constexpr uint32_t kParamsBinding = 1; // the Params block

	// Push constants every shader gets. Keep in sync with ReaShaderInputs in the preamble.
	struct ShaderInputs
	{
		float resolution[2];
		float time;
		float frameRate;
		int32_t frame;
	};

	// One slider: a float (or a vector component) in the shader's Params block.
	// Label, default and range come from a `//@param member 'Label' default min max` annotation.
	struct ShaderParamField
	{
		std::string name;  // "member" or "member.x"
		std::string label; // for display
		float defaultValue = 0.5f;
		float minValue = 0.0f;
		float maxValue = 1.0f;
		uint32_t offset = 0; // bytes, in the Params block
	};

	// A user fragment shader compiled to SPIR-V, with its Params block reflected
	struct CompiledShader
	{
		std::vector<uint32_t> spirv;
		std::vector<ShaderParamField> params;
		uint32_t paramsSize = 0; // bytes
	};

	// Throws std::runtime_error with the compiler's messages (line numbers match `source`)
	CompiledShader compileShader(const std::string& source, const std::string& name);

	// The stored form: { version, paramsSize, params: [...], spirv: [...] }
	nlohmann::json toJson(const CompiledShader& shader);
	CompiledShader fromJson(const nlohmann::json& stored); // throws on a malformed document
} // namespace ReaShader::gpu
