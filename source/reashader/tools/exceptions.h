#pragma once

#include "logging.h"

#define STDEXC const std::exception&

#if defined(_WIN32) || defined(_WIN64) // WINDOWS

// Windows SEH Try-Catch
#include <windows.h>

#define WRAP_LOW_LEVEL_FAULTS(block)                                                                                   \
	__try                                                                                                              \
	{                                                                                                                  \
		block                                                                                                          \
	}                                                                                                                  \
	__except (EXCEPTION_EXECUTE_HANDLER)                                                                               \
	{                                                                                                                  \
		DWORD exceptionCode;                                                                                           \
		LPVOID errorMsgBuff;                                                                                           \
		EXCEPTION_POINTERS* exceptionInfo = GetExceptionInformation();                                                 \
		if (exceptionInfo)                                                                                             \
		{                                                                                                              \
			EXCEPTION_RECORD* exceptionRecord = exceptionInfo->ExceptionRecord;                                        \
			if (exceptionRecord)                                                                                       \
			{                                                                                                          \
				exceptionCode = exceptionRecord->ExceptionCode;                                                        \
				FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, exceptionCode, 0,     \
							  (LPWSTR)&errorMsgBuff, 0, NULL);                                                         \
			}                                                                                                          \
		}                                                                                                              \
		std::ostringstream what;                                                                                       \
		what << "SEH Exception (Code: 0x" << std::hex << exceptionCode << "): ";                                       \
		std::wstring wideErrorMsg(static_cast<const wchar_t*>(errorMsgBuff));                                          \
		std::string errorMsg(wideErrorMsg.begin(), wideErrorMsg.end());                                                \
		what << errorMsg;                                                                                              \
		std::exception e = std::runtime_error(what.str());                                                             \
		LocalFree(errorMsgBuff);                                                                                       \
		throw e;                                                                                                       \
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

#define WRAP_LOW_LEVEL_FAULTS(block)                                                                                   \
	signal(SIGSEGV, signalHandler);                                                                                    \
	block                                                                                                              \
                                                                                                                       \
		if (signal_called)                                                                                             \
	{                                                                                                                  \
		signal_called = false;                                                                                         \
		std::ostringstream what;                                                                                       \
		what << "Signal " << unix_last_signal_code << ": " << unix_last_signal_message;                                \
		std::exception e = std::runtime_error(what.str());                                                             \
		throw e;                                                                                                       \
	}

#endif