/**
 * @file
 * @brief LUTs: .cube files parsed into a 3D table once, on upload, and stored as JSON.
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
	constexpr uint32_t kMaxLutSize = 65; // points per side of a 3D LUT
	constexpr uint32_t kBakedLutSize = 33; // a 1D LUT becomes a cube of this size

	// A 3D LUT over the input domain 0..1: size³ RGB triplets, red index fastest
	// (the .cube order, and the texel order of a Vulkan 3D image)
	struct LutData
	{
		std::string title;
		uint32_t size = 0;
		std::vector<float> rgb;
	};

	// Throws std::runtime_error, with `fileName:line` when a line is at fault.
	// A 1D LUT is baked into a cube, and a DOMAIN other than 0..1 is resampled onto 0..1.
	LutData parseCube(const std::string& text, const std::string& fileName);

	LutData identityLut(uint32_t size);

	// The stored form: { version, title, size, data: base64 of half-float RGB, little endian }
	nlohmann::json toJson(const LutData& lut);
	LutData lutFromJson(const nlohmann::json& stored); // throws on a malformed document
} // namespace ReaShader::gpu
