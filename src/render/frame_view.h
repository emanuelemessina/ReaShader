/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

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
