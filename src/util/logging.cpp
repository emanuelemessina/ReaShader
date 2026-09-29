/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "logging.h"
#include "tools/paths.h"

#include <boxer/boxer.h>

#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	constexpr const char* levelNames[] = { "Info", "Warning", "Error" };

	struct Box
	{
		LogLevel level;
		std::string caption;
		std::string text;
	};

	std::mutex fileMutex;
	std::ofstream logFile; // opened (and truncated) on first use

	std::mutex boxMutex;
	std::vector<Box> queuedBoxes;
	std::map<const void*, std::function<void()>> boxRequesters;

	void writeToFile(const std::string& line)
	{
		std::lock_guard lock(fileMutex);
		if (!logFile.is_open())
		{
			logFile.open(tools::paths::pluginDir() / "rs.log", std::ios::out | std::ios::trunc);
			if (!logFile.is_open())
			{
				std::cerr << "Cannot open rs.log for writing" << std::endl;
				return;
			}
		}
		logFile << line << std::endl;
	}

	void showBox(const Box& box)
	{
		boxer::Style style = box.level == EXCEPTION ? boxer::Style::Error
							 : box.level == WARNING ? boxer::Style::Warning
													: boxer::Style::Info;
		boxer::show(box.text.c_str(), box.caption.c_str(), style);
	}

	void queueBox(Box box)
	{
		std::function<void()> requestMainThread;
		{
			std::lock_guard lock(boxMutex);
			if (!boxRequesters.empty())
			{
				queuedBoxes.push_back(std::move(box));
				requestMainThread = boxRequesters.begin()->second;
			}
		}

		if (requestMainThread)
			requestMainThread();
		else
			showBox(box);
	}
} // namespace

void LOG(LogLevel level, unsigned destinations, std::string_view sender, std::string_view title,
		 std::string_view message)
{
	if (destinations & (toFile | toConsole))
	{
		std::string line = std::format("{:%d-%m-%Y %H:%M:%OS} | [{}] ({}) {} : {}", std::chrono::system_clock::now(),
									   levelNames[level], sender, title, message);
		if (destinations & toFile)
			writeToFile(line);
		if (destinations & toConsole)
			(level == EXCEPTION ? std::cerr : std::cout) << line << std::endl;
	}

	if (destinations & toBox)
		queueBox({ level, std::format("[{}] ({})", levelNames[level], sender), std::format("{} : {}", title, message) });
}

void LOG(const std::exception& e, unsigned destinations, std::string_view sender, std::string_view title,
		 std::string_view message)
{
	LOG(EXCEPTION, destinations, sender, title, std::format("{} | {}", message, e.what()));
}

void registerBoxRequester(const void* owner, std::function<void()> requestMainThreadCallback)
{
	std::lock_guard lock(boxMutex);
	boxRequesters[owner] = std::move(requestMainThreadCallback);
}

void unregisterBoxRequester(const void* owner)
{
	std::lock_guard lock(boxMutex);
	boxRequesters.erase(owner);
}

void showQueuedBoxes()
{
	std::vector<Box> boxes;
	{
		std::lock_guard lock(boxMutex);
		boxes.swap(queuedBoxes);
	}
	for (const Box& box : boxes)
		showBox(box);
}
