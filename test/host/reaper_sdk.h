/**
 * @file
 * @brief The REAPER SDK headers the host needs, in an order that compiles.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

// video_frame.h needs wdltypes.h first. NOMINMAX: <windows.h>'s min/max macros break std::min/std::max
// in the test code that includes this.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "reaper_plugin.h"
#include "wdltypes.h"
#include "video_frame.h"
#include "video_processor.h"
