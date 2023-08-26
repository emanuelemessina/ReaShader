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
	__except (EXCEPTION_EXECUTE_HANDLER)                                                                               \
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