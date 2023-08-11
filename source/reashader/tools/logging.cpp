#include "logging.h"
#include "tools/paths.h"
#include <fstream>

static bool log_files_erase{ true }; // erase all logs on first launch

std::string build_log_line(LogLevel level, const char* sender, const char* title, const char* message)
{
	std::string ret = std::format("{:%d-%m-%Y %H:%M:%OS} | ", std::chrono::system_clock::now()) +
					  std::format("[{}] ({}) {} : {}", logLevelStrings[level], sender, title, message);
	return ret;
}

void console_log(LogLevel level, const char* sender, const char* title, const char* message)
{
	std::ostream* o;
	switch (level)
	{
		case EXCEPTION:
			o = &std::cerr;
			break;
		default:
			o = &std::cout;
			break;
	}
	(*o) << build_log_line(level, sender, title, message) << std::endl;
}
void log_to_file(LogLevel level, const char* sender, const char* title, const char* message)
{
	int openMode{ 0 };
	if (log_files_erase)
	{
		openMode |= std::ios::trunc;
		log_files_erase = false;
	}
	else
		openMode |= std::ios::app;

	std::string filePath = tools::paths::join({ VST3_BUNDLE_DIR, "rs.log" });
	std::ofstream logFile;

	logFile.open(filePath);

	if (!logFile.is_open())
	{
		LOG(WARNING, toConsole | toBox, "Logger", "Filesytem error",
			std::move(std::format("Cannot open {} for writing", filePath)));
		return;
	}

	logFile << build_log_line(level, sender, title, message) << std::endl;

	logFile.close();
}
void show_box(LogLevel level, const char* sender, const char* title, const char* message)
{
	boxer::Style style;
	switch (level)
	{
		case EXCEPTION:
			style = boxer::Style::Error;
			break;
		case WARNING:
			style = boxer::Style::Warning;
			break;
		default:
			style = boxer::Style::Info;
			break;
	}
	boxer::show(std::format("{} : {}", title, message).c_str(),
				std::format("[{}] ({})", logLevelStrings[level], sender).c_str(), style);
}

void _LOG(LogLevel level, LogDestFlags flags, const char* sender, const char* title, const char* message)
{
	if (flags & toFile)
	{
		log_to_file(level, sender, title, message);
	}
	if (flags & toConsole)
	{
		console_log(level, sender, title, message);
	}
	if (flags & toBox)
	{
		show_box(level, sender, title, message);
	}
}
void LOG(LogLevel level, LogDestFlags flags, std::string&& sender, std::string&& title, std::string&& message)
{
	_LOG(level, flags, sender.c_str(), title.c_str(), message.c_str());
}
void LOG(STDEXC e, LogDestFlags flags, std::string&& sender, std::string&& title, std::string&& message)
{
	LOG(EXCEPTION, flags, std::move(sender), std::move(title), std::format("{} | {}", message, e.what()));
}
