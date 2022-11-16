#include "tools/exceptions.h"
#include "tools/paths.h"
#include <fstream>

static bool log_files_erase{ true }; // erase all logs on first launch

std::string&& get_log_line(LogLevel level, const char* sender, const char* title, const char* message){
	return std::move(
			std::format("{:%d-%m-%Y %H:%M:%OS} |", std::chrono::system_clock::now()) +
			std::format("[{}] ({}) : {}", logLevelStrings[level], sender, message)
			);
}

void log_to_file(LogLevel level, const char * sender, const char * title, const char* message){
	
	int openMode{ 0 };
	if (log_files_erase) {
		openMode |= std::ios::trunc;
		log_files_erase = false;
	}
	else
		openMode |= std::ios::app;

	std::string filePath = tools::paths::join({ VST3_BUNDLE_DIR, "rs.log" });
	std::ofstream logFile;
	
	logFile.open(filePath);
	
	if (!logFile.is_open()) {
		boxer::show(std::format("Cannot open {} for writing", filePath).c_str(), "Warning", boxer::Style::Warning);
		return;
	}

	logFile << get_log_line(level, sender, title, message) << std::endl;

	logFile.close();
}
