/**
 * @file
 * @brief FrameView: a video frame in CPU memory.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <cstdint>

namespace ReaShader
{
	// A video frame in CPU memory: BGRA pixels (REAPER's 'RGBA'), rows `rowBytes` apart
	struct FrameView
	{
		int width = 0;
		int height = 0;
		int rowBytes = 0;
		uint8_t* pixels = nullptr;
	};
} // namespace ReaShader
