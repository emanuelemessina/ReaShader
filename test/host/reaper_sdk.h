/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

// The REAPER SDK headers the host needs, in the order they compile in
// (video_frame.h needs wdltypes.h first). NOMINMAX: <windows.h>'s min/max macros break std::min/std::max
// in the test code that includes this.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "reaper_plugin.h"
#include "wdltypes.h"
#include "video_frame.h"
#include "video_processor.h"
