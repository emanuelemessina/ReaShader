/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "logging.h"

#ifndef STDEXC
#define STDEXC const std::exception&
#endif

#if defined(_WIN32) || defined(_WIN64) // WINDOWS

// Windows SEH Try-Catch
#include <windows.h>

// 0xE06D7363 ("msc" encoded into the low 3 bytes) is the SEH exception code the MSVC-compatible
// C++ ABI uses to implement `throw` under the hood -- a bare __except(EXCEPTION_EXECUTE_HANDLER)
// catches this too, silently swallowing ordinary C++ exceptions (turning a readable
// std::runtime_error::what() into an unreadable generic "SEH Exception (Code: 0xE06D7363)") before
// a real catch(...) further up the call stack ever sees them. Confirmed empirically: a
// VK_CHECK_RESULT throw from deep inside _initVulkan() was getting caught (and its message
// discarded) right here instead of by ReaShaderRenderer::init()'s own catch (STDEXC e). Excluding
// this code and continuing the search lets genuine C++ exceptions propagate normally, while still
// catching real low-level faults (access violations, illegal instructions, etc.) as intended.
constexpr DWORD kCxxExceptionCode = 0xE06D7363;

static inline void win_seh_thrower(DWORD code, const char* err_sender, const char* err_title, const char* err_message)
{
	char completeMessage[256];
	sprintf(completeMessage, "%s | SEH Exception (Code: 0x%08X)", err_message, code);
	_LOG(EXCEPTION, toConsole | toFile | toBox, err_sender, err_title, completeMessage);
}

#define WRAP_LOW_LEVEL_FAULTS(try_block, err_sender, err_title, err_message)                                           \
	__try                                                                                                              \
	{                                                                                                                  \
		try_block                                                                                                      \
	}                                                                                                                  \
	__except (GetExceptionCode() == kCxxExceptionCode ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)         \
	{                                                                                                                  \
		DWORD exceptionCode = GetExceptionCode();                                                                      \
		win_seh_thrower(exceptionCode, err_sender, err_title, err_message);                                            \
	}

#else // UNIX

#include <csignal>
#include <cstdlib>

static volatile unix_signal_called = false;
static volatile std::string unix_last_signal_message;
static volatile int unix_last_signal_code;

// Unix-like Systems Signal Handler
void signalHandler(int signal)
{
	unix_last_signal_message = strsignal(signal);
	unix_last_signal_code = signal;
	signal_called = true;
}

// register other signals if needed

#define WRAP_LOW_LEVEL_FAULTS(try_block, err_sender, err_title, err_message)                                           \
	signal(SIGSEGV, signalHandler);                                                                                    \
	block if (signal_called)                                                                                           \
	{                                                                                                                  \
		signal_called = false;                                                                                         \
		std::ostringstream what;                                                                                       \
		what << "Signal " << unix_last_signal_code << ": " << unix_last_signal_message;                                \
		std::exception e = std::runtime_error(what.str());                                                             \
		LOG(e, toConsole | toFile | toBox, err_sender, err_title, err_message);                                        \
	}

#endif