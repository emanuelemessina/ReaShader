/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#ifdef _WIN32

#include "clap/webui_host.h"

#include <mutex>
#include <thread>

#include <windows.h>

#include "webview/webview.h"
#include <nlohmann/json.hpp>

#include "plugin/plugin.h"
#include "util/logging.h"
#include "util/paths.h"

namespace ReaShader
{
	namespace
	{
		std::string fileUrlForRsuiHtml()
		{
			std::u8string path = (util::paths::rsuiDir() / "rsui.html").generic_u8string();
			return "file:///" + std::string(path.begin(), path.end());
		}

		// JSON is valid JS literal syntax: the message is passed to the receiver as an object, no escaping needed
		std::string buildDispatchScript(const std::string& jsonMsg)
		{
			return "window.__reashaderOnMessage && window.__reashaderOnMessage(" + jsonMsg + ");";
		}
	} // namespace

	// `webview` is created and destroyed on `thread`, but read from other threads (resize(), the
	// plugin's sender), so every access holds `mutex`.
	struct WebUIHost::Impl
	{
		ReaShaderPlugin* plugin{ nullptr };
		void* parentHwnd{ nullptr };

		std::thread thread;
		std::mutex mutex;
		std::unique_ptr<webview::webview> webview;
		bool stopRequested{ false }; // set by ~WebUIHost(), possibly before the webview finished constructing
	};

	WebUIHost::WebUIHost(void* parentHwnd, ReaShaderPlugin* plugin) : _impl(std::make_unique<Impl>())
	{
		_impl->plugin = plugin;
		_impl->parentHwnd = parentHwnd;

		_impl->thread = std::thread([this]() {
			HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			bool comInitializedByUs = (hr == S_OK);

			// thread entry point: an escaping exception would std::terminate()
			try
			{
#ifdef NDEBUG
				constexpr bool devTools = false;
#else
				constexpr bool devTools = true; // right click -> Inspect
#endif
				auto wv = std::make_unique<webview::webview>(devTools, _impl->parentHwnd);

				wv->bind("postToNative", [this](const std::string& req) -> std::string {
					// req is the JSON array of the JS call's arguments: ["<message-json>"]
					try
					{
						auto args = nlohmann::json::parse(req);
						if (_impl->plugin && args.is_array() && !args.empty())
							_impl->plugin->handleWebUIMessage(args.at(0).get<std::string>());
					}
					catch (const std::exception& e)
					{
						LOG(WARNING, toConsole | toFile, "WebUIHost", "Malformed message from web UI",
							e.what());
					}
					return "null";
				});

				if (_impl->plugin)
				{
					// the plugin may send from any thread; dispatch() runs the eval() on the webview thread
					_impl->plugin->setWebUISender([this](const std::string& msg) {
						std::lock_guard<std::mutex> lock(_impl->mutex);
						if (!_impl->webview)
							return;
						std::string script = buildDispatchScript(msg);
						_impl->webview->dispatch([this, script = std::move(script)]() {
							std::lock_guard<std::mutex> lock(_impl->mutex);
							if (_impl->webview)
								_impl->webview->eval(script);
						});
					});
				}

				wv->navigate(fileUrlForRsuiHtml());

				// the webview's widget starts at size 0 and doesn't follow the container: size it now
				RECT clientRect{};
				if (GetClientRect((HWND)_impl->parentHwnd, &clientRect))
				{
					auto widget = wv->widget();
					if (widget.ok())
						MoveWindow((HWND)widget.value(), 0, 0, clientRect.right - clientRect.left,
								   clientRect.bottom - clientRect.top, TRUE);
				}

				bool stopRequested;
				{
					std::lock_guard<std::mutex> lock(_impl->mutex);
					_impl->webview = std::move(wv);
					stopRequested = _impl->stopRequested;
				}

				// pump the webview's messages (and dispatch()ed calls) until ~WebUIHost() terminates it
				if (!stopRequested)
					_impl->webview->run();
			}
			catch (const std::exception& e)
			{
				LOG(WARNING, toConsole | toFile | toBox, "WebUIHost", "Failed to create embedded web UI",
					e.what());
			}
			catch (...)
			{
				LOG(WARNING, toConsole | toFile | toBox, "WebUIHost", "Failed to create embedded web UI",
					"unknown error");
			}

			// COM/Win32 objects must be destroyed on the thread that created them
			{
				std::lock_guard<std::mutex> lock(_impl->mutex);
				_impl->webview.reset();
			}

			if (comInitializedByUs)
				CoUninitialize();
		});
	}

	WebUIHost::~WebUIHost()
	{
		if (_impl->plugin)
			_impl->plugin->clearWebUISender();

		{
			std::lock_guard<std::mutex> lock(_impl->mutex);
			_impl->stopRequested = true;
			// terminate() is PostQuitMessage(0) on Win32, which quits the *calling* thread's loop,
			// so it must run on the webview thread. A webview still being built sees stopRequested.
			if (_impl->webview)
				_impl->webview->dispatch([wv = _impl->webview.get()]() { wv->terminate(); });
		}

		if (_impl->thread.joinable())
		{
			// the webview's windows are children of our UI-thread container and their teardown can
			// SendMessage() to it: keep servicing sent messages while waiting, or join() deadlocks
			HANDLE threadHandle = (HANDLE)_impl->thread.native_handle();
			while (MsgWaitForMultipleObjects(1, &threadHandle, FALSE, INFINITE, QS_SENDMESSAGE) == WAIT_OBJECT_0 + 1)
			{
				MSG msg;
				PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
			}
			_impl->thread.join();
		}
	}

	void WebUIHost::resize(int width, int height)
	{
		std::lock_guard<std::mutex> lock(_impl->mutex);
		if (!_impl->webview)
			return;

		// Resizing the webview's own widget window makes it resize the WebView2 controller. It runs
		// on the webview thread because MoveWindow on another thread's window blocks the caller.
		_impl->webview->dispatch([wv = _impl->webview.get(), width, height]() {
			auto widget = wv->widget();
			if (widget.ok())
				MoveWindow((HWND)widget.value(), 0, 0, width, height, TRUE);
		});
	}
} // namespace ReaShader

#endif // _WIN32
