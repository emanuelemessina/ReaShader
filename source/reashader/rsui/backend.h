#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include <any>

using controller = Steinberg::Vst::EditControllerEx1;

namespace ReaShader {
	class RSUIServer {
	public:
		RSUIServer(controller* rsController);
		~RSUIServer();

		RSUIServer(const RSUIServer&) = delete;
		RSUIServer& operator= (const RSUIServer&) = delete;

		bool hasError() { return _failed; }
		uint16_t getPort() const { return _port; }
		void sendWSMessage(const char* msg);

	private:
		uint16_t _port;
		std::any _uiserver_handle{ nullptr };
		std::any _ws_registry;
		controller* _rsController;
		bool _failed{ false };
		auto _uiserver_handler();
		void _update_param(const char* json);
	};
}