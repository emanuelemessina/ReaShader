#pragma once

#include <chrono>
#include <format>
#include <iostream>

#define LOG_CERR_EXCEPTION(e, sender, title) \
std::cerr << ( \
std::format("{:%d-%m-%Y %H:%M:%OS}", std::chrono::system_clock::now()) \
.append("[Exception] (").append(sender).append(") ").append(title).append(" : ") + e.what() \
).c_str() << std::endl;

#define LOG_FILE_EXCEPTION(e, sender, title) \

#define LOG_EXCEPTION(e, sender, title) LOG_CERR_EXCEPTION(e, sender, title) LOG_FILE_EXCEPTION(e, sender, title)

#include "boxer/boxer.h"

#define	BOX_SHOW_EXCEPTION(e, sender, title) \
boxer::show( \
std::string(title).append(" : ").append(e.what()).c_str(),  \
std::string("[Exception] (").append(sender).append(")").c_str(), boxer::Style::Error);

#define EXCEPTION_OUT(e, sender, title) LOG_EXCEPTION(e, sender, title) BOX_SHOW_EXCEPTION(e, sender, title)

#define STDEXC const std::exception&