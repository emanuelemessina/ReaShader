#pragma once

#include "boxer/boxer.h"

#include "tools/flagoperators.h"
#include <chrono>
#include <format>
#include <iostream>

#define STDEXC const std::exception&


enum LogLevel
{
	INFO,
	WARNING,
	EXCEPTION
};

static const char* logLevelStrings[] = { "Info", "Warning", "Error" };

enum LogDestFlags : int8_t
{
	toFile = 0b1,
	toConsole = 0b10,
	toBox = 0b100
};

void LOG(LogLevel level, LogDestFlags flags, std::string&& sender, std::string&& title, std::string&& message);
void LOG(STDEXC e, LogDestFlags flags, std::string&& sender, std::string&& title, std::string&& message);

// ----------------------------------------------------------
