/**
 * @file
 * @brief Base64 (standard alphabet, padded), for binary data inside JSON.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace util::base64
{
	std::string encode(std::span<const uint8_t> bytes);
	std::vector<uint8_t> decode(std::string_view text); // throws std::runtime_error on invalid input
} // namespace util::base64
