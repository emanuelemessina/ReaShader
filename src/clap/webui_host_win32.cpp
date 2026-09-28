/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#ifdef _WIN32

#include "webui_host_win32.h"

#include <mutex>
#include <thread>

#include <windows.h>

#include "webview/webview.h"

#include "reashaderplugin.h"
#include "tools/logging.h"
#include "tools/paths.h"

namespace ReaShader
{
	namespace
	{
		std::string fileUrlForRsuiHtml()
		{
			std::string path = tools::paths::join({ RSUI_DIR, "rsui.html" });
			for (auto& c : path)
			{
				if (c == '\\')
					c = '/';
			}
			return "file:///" + path;
		}

		// msg is a full JSON document (already valid JS object/array/string/number/bool/null literal
		// syntax) -- rather than escaping it into a JS string literal on the C++ side, it's embedded
		// verbatim as a JS literal and re-stringified via JSON.stringify() in JS, so client.js's
		// existing MessageHandler(event.data) code (which expects event.data to be a JSON *string*,
		// same as it got from the old WebSocket's event.data) keeps working unchanged.
		std::string buildDispatchScript(const std::string& jsonMsg)
		{
			return "window.__reashaderOnMessage && window.__reashaderOnMessage({data: JSON.stringify(" + jsonMsg +
				   ")});";
		}
	} // namespace

	// _webview and comInitializedByUs are only ever touched while holding _mutex -- constructed and
	// destroyed on _thread, but resize()/the ReaShaderPlugin sender callback read/dispatch into it
	// from whichever thread calls them (the UI thread, or ReaShaderRenderer's init thread).
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

		// webview::webview's constructor blocks -- pumping its own nested message loop -- for however
		// long WebView2's async environment/controller setup takes (observed: multiple seconds, worse
		// with more than one plugin instance sharing the WebView2 profile/browser process). Running
		// that on REAPER's UI thread crashed REAPER outright, consistently, regardless of when it was
		// started (confirmed empirically via Windows' own crash records: reaper.exe faulting at the
		// same internal offset every time, exception 0x40000015/STATUS_FATAL_APP_EXIT -- REAPER's own
		// code hitting an assertion from being re-entered while blocked, not memory corruption in our
		// code). So none of this runs on the calling thread at all: it all happens on a dedicated
		// background thread with its own message loop, and every call into the webview after
		// construction goes through webview::webview::dispatch() (documented as safe from any thread)
		// to marshal onto that thread instead.
		_impl->thread = std::thread([this]() {
			HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			bool comInitializedByUs = (hr == S_OK);

			// broad catch: this lambda is the entry point of a thread with no caller to propagate an
			// exception to -- an escaping exception here is std::terminate(), not a catchable failure
			try
			{
				auto wv = std::make_unique<webview::webview>(/* debug */ false, _impl->parentHwnd);

				wv->bind("postToNative", [this](const std::string& req) -> std::string {
					// req is a JSON array of the arguments the JS side passed, e.g. ["<message-json>"]
					// -- invoked by the webview's own message loop, i.e. already on our background
					// thread, same as everything else in this lambda's enclosing scope.
					try
					{
						json args = json::parse(req);
						if (_impl->plugin && args.is_array() && !args.empty())
							_impl->plugin->handleWebUIMessage(args.at(0).get<std::string>());
					}
					catch (const std::exception& e)
					{
						LOG(WARNING, toConsole | toFile, "WebUIHost", "Malformed message from web UI",
							std::string(e.what()));
					}
					return "null";
				});

				if (_impl->plugin)
				{
					// _webuiSend(...) (and therefore this sender) can be invoked from any thread --
					// e.g. ReaShaderRenderer's device enumeration inside activate() calls
					// setRenderingDevicesList() -> _webuiSend(...) synchronously, and CLAP hosts
					// commonly call activate() from a non-UI thread. dispatch() marshals the actual
					// eval() onto the background thread regardless of the caller.
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

				// the embedded webview's internal widget starts at (0,0,0,0) and is never auto-sized
				// to our container (see the resize() comment below) -- without this, nothing would be
				// visible until the host happened to resize the FX window (confirmed empirically).
				// Done directly (not via resize()/dispatch()) since we're already on this thread.
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

				// this thread's entire job from here on: pump messages for the webview's own windows
				// (and anything dispatch()'d to it) until ~WebUIHost() makes it terminate()
				if (!stopRequested)
					_impl->webview->run();
			}
			catch (const std::exception& e)
			{
				LOG(WARNING, toConsole | toFile | toBox, "WebUIHost", "Failed to create embedded web UI",
					std::string(e.what()));
			}
			catch (...)
			{
				LOG(WARNING, toConsole | toFile | toBox, "WebUIHost", "Failed to create embedded web UI",
					std::string("unknown error"));
			}

			// destroy on the same thread that created it (proper cleanup of its COM/Win32 objects)
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
			// the Win32 backend's terminate() is a bare PostQuitMessage(0), which quits the *calling*
			// thread's loop -- called from here it would post WM_QUIT to REAPER's UI thread and leave
			// ours running forever (join() below never returning). So it has to run on our thread.
			// If the webview is still constructing, the thread sees stopRequested and skips run().
			if (_impl->webview)
				_impl->webview->dispatch([wv = _impl->webview.get()]() { wv->terminate(); });
		}

		if (_impl->thread.joinable())
		{
			// the webview's windows are children of our container, which belongs to this (the UI)
			// thread -- tearing them down can SendMessage() to it, so keep servicing sent messages
			// while waiting instead of blocking in a plain join()
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

		// the webview library's own internal "widget" child window (distinct from our container
		// m_window, which is what we passed in) is what its own WM_SIZE handler resizes the actual
		// WebView2 controller against -- MoveWindow triggers that chain (confirmed from
		// win32_edge_engine's constructor/resize_webview() in webview.h: nothing auto-tracks our
		// container's size, since the library never subclasses a caller-supplied parent window).
		// Dispatched rather than done inline: the widget HWND belongs to the background thread's
		// message queue, and MoveWindow on a window owned by another thread blocks the caller until
		// that thread's loop services it -- dispatch() avoids blocking the caller (typically REAPER's
		// UI thread, via the container's WM_SIZE handler) on that at all.
		_impl->webview->dispatch([wv = _impl->webview.get(), width, height]() {
			auto widget = wv->widget();
			if (widget.ok())
				MoveWindow((HWND)widget.value(), 0, 0, width, height, TRUE);
		});
	}
} // namespace ReaShader

#endif // _WIN32
