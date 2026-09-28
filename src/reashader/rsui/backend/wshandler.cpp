/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#ifdef _WIN32
#include <SDKDDKVer.h> // don't know why triggers warning without
#endif

#include <restinio/all.hpp> // must be included in this translation unit otherwise conflict with windows.h
#include <restinio/websocket/websocket.hpp> // must be included right after restinio/all .. (otherwise syntax error wtf)
namespace rws = restinio::websocket::basic;
using ws_registry_t = std::map<std::uint64_t, rws::ws_handle_t>;

// -----------------------------------------------------

#include <nlohmann/json.hpp>

#include "backend.h"

#include "tools/mime_types.h"
#include "tools/paths.h"

#include "backend.h"

#include "tools/exceptions.h"
#include "tools/logging.h"

namespace ReaShader
{

	/**
	 * Send broadcast message to each ws connection (from server to browser).
	 */
	void RSUIServer::sendWSTextMessage(std::string& msg)
	{
		_sendWSMessage(msg, false);
	}
	void RSUIServer::sendWSBinaryMessage(std::vector<char>& msg)
	{
		std::string str(msg.data(), msg.size());
		_sendWSMessage(str, true);
	}

	void RSUIServer::_sendWSMessage(std::string& msg, bool binary)
	{
		auto message =
			rws::message_t(rws::final_frame, binary ? rws::opcode_t::binary_frame : rws::opcode_t::text_frame, msg);
		for (const auto& [id, handle] : *std::any_cast<ws_registry_t*>(_ws_registry))
		{
			handle->send_message(message);
		}
	}

	FileUploadProcess::~FileUploadProcess()
	{
		forceStopFlag.store(true); // someone removed us from the map, thread will exit for sure
		timeOutWatchDog.detach();
		// restore the current upload connection if there are no more uploads from this connection
		if (serv->fileUploadProcessesRegistry.size() == 1)
			serv->currentFileUploadConnectionId = 0;
	}

	bool FileUploadProcess::addChunk(const std::string& chunk, bool continuation)
	{
		lastPacketTime.store(std::chrono::steady_clock::now());

		if (receivedPacketCount == expectedPacketCount && !continuation) // this is the closing chunk
		{
			// restore continuation id
			serv->continuationUid32 = 0;

			forceStopFlag.store(true); // prevent the watchdog from kicking us out
			
			if (chunk.size() == sizeof(uint32_t))
			{
				// correct closing chunk
				return true;
			}
			// wrong closing chunk
			// notify the client
			auto it = std::any_cast<ws_registry_t*>(serv->_ws_registry)->find(serv->currentFileUploadConnectionId);
			if (it != std::any_cast<ws_registry_t*>(serv->_ws_registry)->end())
			{
				const auto& originalClientHandle = it->second;
				json error;
				error["uid32"] = uid32;
				error["status"] = "error";
				error["message"] = "Wrong closing chunk";
				originalClientHandle->send_message(
					rws::message_t(rws::final_frame, rws::opcode_t::text_frame, error.dump()));
			}
			// interrupt this file upload
			serv->fileUploadProcessesRegistry.erase(uid32);
			return false;
		}

		bufferedData.insert(bufferedData.end(), chunk.begin() + (continuation ? 0 : sizeof(uint32_t)), chunk.end());

		// send proceed to step the frontend forward
		json proceed;
		proceed["uid32"] = uid32;
		proceed["status"] = "proceed";
		ws_registry_t& wsr = *std::any_cast<ws_registry_t*>(serv->_ws_registry);
		wsr[serv->currentFileUploadConnectionId]->send_message(rws::message_t(rws::final_frame, rws::opcode_t::text_frame, proceed.dump()));

		if (continuation)
			return false;

		// save the continuation id
		serv->continuationUid32 = uid32;

		receivedPacketCount++;

		return false;
	}

	void FileUploadProcess::_timeOutCheck()
	{
		// initial packet time
		lastPacketTime.store(std::chrono::steady_clock::now());

		auto elapsedSeconds = 0;

		while (elapsedSeconds < FRONTEND_RESPONSE_TIMEOUT && !forceStopFlag.load())
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));

			auto currentTime = std::chrono::steady_clock::now();
			elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastPacketTime.load()).count();
		}

		if (forceStopFlag.load()) // server is shutting down
			return;

		// Timeout occurred

		// notify the client
		auto it = std::any_cast<ws_registry_t*>(serv->_ws_registry)->find(serv->currentFileUploadConnectionId);
		if (it != std::any_cast<ws_registry_t*>(serv->_ws_registry)->end())
		{
			const auto& originalClientHandle = it->second;
			json error;
			error["uid32"] = uid32;
			error["status"] = "error";
			error["message"] = "File upload timeout";
			originalClientHandle->send_message(rws::message_t(rws::final_frame, rws::opcode_t::text_frame, error.dump()));
		}

		// remove the file upload process from the map (will call the destructor)
		serv->fileUploadProcessesRegistry.erase(uid32);
	}

	uint32_t fileuploadprocess_chunk_get_uid32(const std::string& chunk)
	{
		uint32_t uid32 = 0;
		// little endian
		for (size_t i = 0; i < sizeof(uint32_t); ++i)
		{
			uid32 |= static_cast<uint8_t>(chunk[i]) << (8 * i);
		}
		return uid32;
	}

	void RSUIServer::_ws_handler(std::any ws_handle, std::any ws_message)
	{
		rws::ws_handle_t wsh = std::any_cast<rws::ws_handle_t>(ws_handle);
		rws::message_handle_t m = std::any_cast<rws::message_handle_t>(ws_message);

		const auto& payload = m->payload();

		if (rws::opcode_t::text_frame == m->opcode())
		{
			_incomingTextMessageHandler(std::string(payload.c_str()));
		}
		else if (rws::opcode_t::binary_frame == m->opcode())
		{
			// priority channel for normal binary messages
			if (has_binary_magic(payload))
			{
				// just forward it
				std::vector<char> charVector(payload.begin(), payload.end());
				_incomingBinaryMessageHandler(std::move(charVector));
				return;
			}

			// if not normal, must be fileupload

			// check for current upload process
			if (currentFileUploadConnectionId != 0) // someone is uploading
			{
				// tell other connections that we are busy, only one connection id can upload at a time
				if (currentFileUploadConnectionId != wsh->connection_id())
				{
					// since any attempt of uploading would have been blocked by this case, we are sure this is the upload header

					const auto header = nlohmann::json::parse(payload);

					uint32_t uid32 = header["uid32"].get<uint32_t>();

					json busy;
					busy["uid32"] = uid32;
					busy["status"] = "busy";
					wsh->send_message(rws::message_t(rws::final_frame, rws::opcode_t::text_frame, busy.dump()));
					return;
				}
				// the id is the same, good to go
			}
			
			// get the first 32bits
			uint32_t uid32 = fileuploadprocess_chunk_get_uid32(payload);

			// check if there is a file upload in progress for this id

			auto it = fileUploadProcessesRegistry.find(uid32);
			if (it != fileUploadProcessesRegistry.end())
			{
				// a fileupload process exists, so this is the next chunk
	
				FileUploadProcess& fup = *it->second;
				if (fup.addChunk(payload))
				{
					// finished
					
					// send the file
					_incomingFileHandler(std::move(fup.getMetadata()), std::move(fup.getName()),
										 std::move(fup.getExtension()), fup.getSize(), std::move(fup.getData()));
					// remove process from map
					fileUploadProcessesRegistry.erase(uid32);
				}
				
				return;
			}

			// no process exists, then it must be the process start header

			try
			{
				const auto header = nlohmann::json::parse(payload);

				// save the connection id
				// even if this is a new upload, if we arrived here it's because this is the same connection, so worst case no change
				currentFileUploadConnectionId = wsh->connection_id();

				uint32_t uid32 = header["uid32"].get<uint32_t>();

				// check that there is not a process already for this uid!
				// this is a double start
				auto it = fileUploadProcessesRegistry.find(uid32);
				if (it != fileUploadProcessesRegistry.end())
				{
					// we just want to kill this upload
					fileUploadProcessesRegistry.erase(uid32);
					json error;
					error["uid32"] = uid32;
					error["status"] = "error";
					error["message"] = "Started a concurrent upload for the same file!";
					wsh->send_message(rws::message_t(rws::final_frame, rws::opcode_t::text_frame, error.dump()));
					return;
				}
				
				fileUploadProcessesRegistry.emplace(
					uid32, std::make_unique<FileUploadProcess>(this, uid32, header["numPackets"]));

				FileUploadProcess& fup = *fileUploadProcessesRegistry[uid32];

				// save header
				fup.setFileInfo(header["filename"], header["extension"], header["mimetype"] , header["size"] ,
								header["metadata"]);
					
				// Send acknowledgment to the client, it can start the chunk uploads
				json proceed;
				proceed["uid32"] = uid32;
				proceed["status"] = "proceed";
				wsh->send_message(rws::message_t(rws::final_frame, rws::opcode_t::text_frame, proceed.dump()));
				return;
			}
			// if it's not even the upload header then it's wrong,
			// kill all uploads
			// send a broadcast error message for pending uploads
			catch (STDEXC e)
			{
				fileUploadProcessesRegistry.clear();

				json error;
				error["uid32"] = -1;
				error["status"] = "error";
				error["message"] = e.what();
				wsh->send_message(rws::message_t(rws::final_frame, rws::opcode_t::text_frame, error.dump()));
				return;
			}

		}
		else if (rws::opcode_t::continuation_frame == m->opcode())
		{
			// messages longer than 64k are fragmented, we assume they are binary

			// check if the process still exists
			auto it = fileUploadProcessesRegistry.find(continuationUid32);
			if (it != fileUploadProcessesRegistry.end())
			{
				FileUploadProcess& fup = *fileUploadProcessesRegistry[continuationUid32];
				fup.addChunk(payload, true); // we are sure that a closing chunk never gets fragmented because it's only 4 bytes, so this is not the final chunk					
				return;
			}
			// if it does not, ignore it
		}
		else if (rws::opcode_t::ping_frame == m->opcode())
		{
			auto resp = *m;
			resp.set_opcode(rws::opcode_t::pong_frame);
			wsh->send_message(resp);
		}
		else if (rws::opcode_t::connection_close_frame == m->opcode())
		{
			std::any_cast<ws_registry_t*>(_ws_registry)->erase(wsh->connection_id());
		}
	}

} // namespace ReaShader
