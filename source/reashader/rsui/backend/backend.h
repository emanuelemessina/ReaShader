#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include <any>
#include <functional>
#include <string>
#include <vector>

using IncomingTextMessageHandler = std::function<void(const std::string&)>;
using IncomingBinaryMessageHandler = std::function<void(const std::vector<char>&)>;

namespace ReaShader
{
	/**
	 * @brief Starts server upon construction
	 * @param incomingTextMessageHandler pointer to callback that will handle incoming text messages from webui
	 * @param incomingBinaryMessageHandler pointer to callback that will handle incoming binary messages from webui
	 */
	class RSUIServer
	{
	  public:
		RSUIServer(IncomingTextMessageHandler incomingTextMessageHandler,
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
		void _sendWSMessage(std::string& msg, bool binary);
		IncomingTextMessageHandler _incomingTextMessageHandler;
		IncomingBinaryMessageHandler _incomingBinaryMessageHandler;
	};
} // namespace ReaShader