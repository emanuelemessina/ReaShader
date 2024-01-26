/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include <any>
#include <functional>
#include <string>
#include <vector>
#include <thread>

#include "rsui/api.h"

using IncomingTextMessageHandler = std::function<void(const std::string&&)>;
using IncomingBinaryMessageHandler = std::function<void(const std::vector<char>&&)>;
using IncomingFileHandler = std::function<void(json&& metadata, std::string&& name, std::string&& extension, size_t size, const std::vector<char>&& data)>;

// THE STD ANY ARE BECAUSE OF RESTINIO FUCKING HEADERS CONFLICTING WITH Windows.h AND HAVING SYNTAX ERRORS IF MISPLACED
// so we have to include the headers in the cpp and we cannot have restinio types in here

namespace ReaShader
{
	// ------------------------------------------------------

	FWD_DECL(RSUIServer)

	#define FRONTEND_RESPONSE_TIMEOUT 60 // in seconds

	struct FileUploadProcess
	{
		FileUploadProcess(RSUIServer* serv, uint32_t uid32, std::size_t expectedPacketCount)
			: serv(serv), uid32(uid32), expectedPacketCount(expectedPacketCount)
		{
			// start timeout watchdog
			timeOutWatchDog = std::thread(&FileUploadProcess::_timeOutCheck, this);
		}
		~FileUploadProcess();

		void setFileInfo(std::string name, std::string extension, std::string mimeType, size_t size, json metadata)
		{
			this->name = name;
			this->extension = extension;
			this->mimeType = mimeType;
			this->size = size;
			this->metadata = metadata;
		}
		std::string& getName()
		{
			return name;
		}
		std::string& getExtension()
		{
			return extension;
		}
		std::string& getMimeType()
		{
			return mimeType;
		}
		size_t getSize()
		{
			return size;
		}
		json& getMetadata()
		{
			return metadata;
		}
		std::vector<char>& getData()
		{
			return bufferedData;
		}

		// automatically strips the uid32 at the beginning unless continuation set (continuation does not increase the received packet count)
		// returns true when that was the last chunk
		bool addChunk(const std::string& chunk, bool continuation = false);

		// buffer
		std::vector<char> bufferedData;
		
		private:
		// file properties
		std::string name;
		std::string extension;
		std::string mimeType;
		size_t size;
		json metadata;
		// link
		RSUIServer* serv;
		uint32_t uid32;
		// control
		std::atomic<bool> forceStopFlag{ false };
		std::size_t receivedPacketCount = 0;
		std::size_t expectedPacketCount = 0;
		std::atomic<std::chrono::steady_clock::time_point> lastPacketTime;
		std::thread timeOutWatchDog;
		
		void _timeOutCheck();
	};

	/**
	 * @brief Starts server upon construction
	 * @param incomingTextMessageHandler pointer to callback that will handle incoming text messages from webui
	 * @param incomingBinaryMessageHandler pointer to callback that will handle incoming binary messages from webui
	 */
	class RSUIServer
	{
	  public:
		RSUIServer(IncomingTextMessageHandler incomingTextMessageHandler, IncomingFileHandler incomingFileHandler,
				   IncomingBinaryMessageHandler incomingBinaryMessageHandler);
		~RSUIServer();

		RSUIServer(const RSUIServer&) = delete;
		RSUIServer& operator=(const RSUIServer&) = delete;

		bool hasError()
		{
			return _failed;
		}
		uint16_t getPort() const
		{
			return _port;
		}

		void sendWSTextMessage(std::string& msg);
		void sendWSBinaryMessage(std::vector<char>& msg);


	  private:
		uint16_t _port;
		std::any _uiserver_handle{ nullptr };
		std::any _ws_registry;
		bool _failed{ false };
		auto _uiserver_handler();
		void _ws_handler(std::any wsh, std::any m);
		void _sendWSMessage(std::string& msg, bool binary);
		IncomingTextMessageHandler _incomingTextMessageHandler;
		IncomingFileHandler _incomingFileHandler;
		IncomingBinaryMessageHandler _incomingBinaryMessageHandler;

		const std::string binaryMagic{ "RS" };
		inline bool has_binary_magic(const std::string& payload)
		{
			if (payload.size() < 2)
				// malformed: too short
				return false;

			char magicSequence[3];
			magicSequence[0] = payload[0];
			magicSequence[1] = payload[1];
			magicSequence[2] = payload[2];

			if (magicSequence[2] != '\0')
				// malformed: wrong magic terminator
				return false;

			return std::string(magicSequence) == binaryMagic;
		}

		friend class FileUploadProcess;

		uint64_t currentFileUploadConnectionId{ 0 };
		uint32_t continuationUid32{ 0 };
		std::map<uint32_t, std::unique_ptr<FileUploadProcess>> fileUploadProcessesRegistry;

	};
} // namespace ReaShader