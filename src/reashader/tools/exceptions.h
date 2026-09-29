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

// TODO: untested on macOS/Linux

#include <csetjmp>
#include <csignal>
#include <cstring>
#include <format>

namespace faults
{
	// jump target of the guarded block running on this thread, if any
	inline thread_local sigjmp_buf* activeJump = nullptr;
	inline thread_local volatile sig_atomic_t lastSignal = 0;

	inline void onFault(int signal)
	{
		if (activeJump)
		{
			lastSignal = signal;
			siglongjmp(*activeJump, 1);
		}
		// fault outside a guarded block: crash as usual
		std::signal(signal, SIG_DFL);
		std::raise(signal);
	}

	// Installs the fault handlers for the lifetime of a guarded block, then restores the previous ones
	struct Guard
	{
		static constexpr int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
		struct sigaction previous[std::size(signals)]{};

		Guard()
		{
			struct sigaction action{};
			action.sa_handler = onFault;
			action.sa_flags = SA_NODEFER;
			sigemptyset(&action.sa_mask);
			for (size_t i = 0; i < std::size(signals); i++)
				sigaction(signals[i], &action, &previous[i]);
		}

		~Guard()
		{
			activeJump = nullptr;
			for (size_t i = 0; i < std::size(signals); i++)
				sigaction(signals[i], &previous[i], nullptr);
		}
	};

	inline void logFault(int signal, const char* sender, const char* title, const char* message)
	{
		LOG(EXCEPTION, toConsole | toFile | toBox, sender, title,
			std::format("{} | Signal {} ({})", message, signal, strsignal(signal)));
	}
} // namespace faults

// Like SEH, a fault jumps out of `block` without running its destructors.
#define WRAP_LOW_LEVEL_FAULTS(block, sender, title, message)                                                         \
	{                                                                                                                \
		faults::Guard faultGuard;                                                                                    \
		sigjmp_buf faultJump;                                                                                        \
		if (sigsetjmp(faultJump, 1) == 0)                                                                            \
		{                                                                                                            \
			faults::activeJump = &faultJump;                                                                         \
			block                                                                                                    \
		}                                                                                                            \
		else                                                                                                         \
		{                                                                                                            \
			faults::logFault(faults::lastSignal, sender, title, message);                                            \
		}                                                                                                            \
	}

#endif
