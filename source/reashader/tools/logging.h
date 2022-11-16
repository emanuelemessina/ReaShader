#pragma once

#include <format>
#include <iostream>
#include "boxer/boxer.h"
#include <chrono>

enum LogLevel {
	INFO,
	WARNING,
	EXCEPTION
};

static const char* logLevelStrings[] = { "Info", "Warning", "Error" };

std::string&& get_log_line(LogLevel level, const char* sender, const char* title, const char* message);
void log_to_file(LogLevel level, const char* sender, const char* title, const char* message);

// ----------------------------------------------------------

// EXCEPTIONS

#define STDEXC const std::exception&

#define LOG_CERR_EXCEPTION(e, sender, title) \
std::cerr << get_log_line(EXCEPTION, sender, title, e.what()).c_str() << std::endl;

#define LOG_FILE_EXCEPTION(e, sender, title) log_to_file(EXCEPTION, sender, title, e.what());

#define LOG_EXCEPTION(e, sender, title) LOG_CERR_EXCEPTION(e, sender, title) LOG_FILE_EXCEPTION(e, sender, title)

#define	BOX_SHOW_EXCEPTION(e, sender, title) \
boxer::show( std::format("{} : {}", title, e.what()).c_str(), \
std::format("[{}] ({})", logLevelStrings[EXCEPTION], sender).c_str(), \
boxer::Style::Error);

#define EXCEPTION_OUT(e, sender, title) LOG_EXCEPTION(e, sender, title) BOX_SHOW_EXCEPTION(e, sender, title)

// ----------------------------------------------------------

// WARNINGS

#define LOG_WARNING(sender, title, message) \
std::cout << get_log_line(WARNING, sender, title, message).c_str() << std::endl; \
log_to_file(WARNING, sender, title, message);
