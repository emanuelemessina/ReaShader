/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "logging.h"

// WRAP_LOW_LEVEL_FAULTS(block, sender, title, message):
// runs `block`, catching hardware faults (access violations, illegal instructions, ...)
// and logging them instead of crashing the host.

#ifdef _WIN32

#include <format>
#include <windows.h>

// SEH code the C++ runtime uses for `throw`: let C++ exceptions pass through to their own catch
constexpr DWORD kCxxExceptionCode = 0xE06D7363;

inline void logSehFault(DWORD code, const char* sender, const char* title, const char* message)
{
	LOG(EXCEPTION, toConsole | toFile | toBox, sender, title,
		std::format("{} | SEH Exception (Code: 0x{:08X})", message, code));
}

#define WRAP_LOW_LEVEL_FAULTS(block, sender, title, message)                                                         \
	__try                                                                                                            \
	{                                                                                                                \
		block                                                                                                        \
	}                                                                                                                \
	__except (GetExceptionCode() == kCxxExceptionCode ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)       \
	{                                                                                                                \
		logSehFault(GetExceptionCode(), sender, title, message);                                                     \
	}

#else

// TODO: catch hardware faults on macOS/Linux (signal handlers); for now the block runs unguarded
#define WRAP_LOW_LEVEL_FAULTS(block, sender, title, message) block

#endif
