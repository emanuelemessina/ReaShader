/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <exception>
#include <functional>
#include <string_view>

enum LogLevel
{
	INFO,
	WARNING,
	EXCEPTION
};

// Destinations, combinable: toConsole | toFile | toBox
enum LogDest : unsigned
{
	toFile = 1 << 0,
	toConsole = 1 << 1,
	toBox = 1 << 2
};

// Log line: "<time> | [<level>] (<sender>) <title> : <message>"
// File destination: <plugin dir>/rs.log, truncated on the first write of each process.
void LOG(LogLevel level, unsigned destinations, std::string_view sender, std::string_view title,
		 std::string_view message);
void LOG(const std::exception& e, unsigned destinations, std::string_view sender, std::string_view title,
		 std::string_view message);

// Message boxes (toBox):
// - modal, so they must not block REAPER's audio/video threads
// - queued, then shown on the main thread by showQueuedBoxes()
// - each plugin instance registers a requester that makes the host call it back on the main thread
//   (no requester registered: the box is shown immediately)
void registerBoxRequester(const void* owner, std::function<void()> requestMainThreadCallback);
void unregisterBoxRequester(const void* owner);
void showQueuedBoxes();
