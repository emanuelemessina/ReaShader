/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// clap.gui for Win32 (embedded only): a resizable child window filled by the WebUIHost webview.
// TODO: macOS/Linux.
//
// create() only registers the window class. The window is created in set_parent(), because a
// WS_CHILD window needs its real parent at creation (null parent -> ERROR_TLW_WITH_WSCHILD).
// It is created synchronously there, since the host calls show() right after.

#ifdef _WIN32

#include <cstring>
#include <string>

#include <windows.h>

#include "plugin_state.h"
#include "tools/logging.h"
#include "webui_host_win32.h"

namespace ReaShader
{
	namespace
	{
		constexpr wchar_t kWindowClassName[] = L"ReaShaderGuiWindow";

		// initial size fitting rsui's layout (500px max-width panel); resizable afterwards
		constexpr uint32_t kDefaultWidth = 560;
		constexpr uint32_t kDefaultHeight = 720;
		constexpr uint32_t kMinWidth = 320;
		constexpr uint32_t kMinHeight = 400;

		LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			if (msg == WM_SIZE)
			{
				auto* state = reinterpret_cast<ClapPluginState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
				if (state && state->webUIHost)
					state->webUIHost->resize(LOWORD(lParam), HIWORD(lParam));
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		HMODULE thisModuleHandle()
		{
			HMODULE hm = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								(LPCWSTR)&thisModuleHandle, &hm);
			return hm;
		}

		// returns true if the class is registered (either just now, or already was)
		bool ensureClassRegistered()
		{
			static bool registered = false;
			if (registered)
				return true;

			WNDCLASSW wc{};
			wc.lpfnWndProc = WndProc;
			wc.hInstance = thisModuleHandle();
			wc.lpszClassName = kWindowClassName;
			wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

			ATOM atom = RegisterClassW(&wc);
			if (atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
			{
				registered = true;
				return true;
			}
			return false;
		}

		bool is_api_supported(const clap_plugin_t*, const char* api, bool is_floating)
		{
			return !is_floating && api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
		}

		bool get_preferred_api(const clap_plugin_t*, const char** api, bool* is_floating)
		{
			*api = CLAP_WINDOW_API_WIN32;
			*is_floating = false;
			return true;
		}

		bool gui_create(const clap_plugin_t*, const char* api, bool is_floating)
		{
			if (is_floating || !api || std::strcmp(api, CLAP_WINDOW_API_WIN32) != 0)
				return false;

			return ensureClassRegistered();
		}

		void gui_destroy(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);

			delete state->webUIHost; // tears down the embedded webview first (clears the plugin's sender)
			state->webUIHost = nullptr;
			if (state->guiHwnd)
			{
				DestroyWindow((HWND)state->guiHwnd);
				state->guiHwnd = nullptr;
			}
		}

		bool set_scale(const clap_plugin_t*, double)
		{
			return true; // win32 uses physical pixels; nothing to do
		}

		bool get_size(const clap_plugin_t*, uint32_t* width, uint32_t* height)
		{
			*width = kDefaultWidth;
			*height = kDefaultHeight;
			return true;
		}

		bool can_resize(const clap_plugin_t*)
		{
			return true;
		}

		bool get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
		{
			*hints = {};
			hints->can_resize_horizontally = true;
			hints->can_resize_vertically = true;
			hints->preserve_aspect_ratio = false;
			return true;
		}

		bool adjust_size(const clap_plugin_t*, uint32_t* width, uint32_t* height)
		{
			if (*width < kMinWidth)
				*width = kMinWidth;
			if (*height < kMinHeight)
				*height = kMinHeight;
			return true;
		}

		bool set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			if (!state->guiHwnd)
				return false;

			// the container's WM_SIZE forwards the new size to the webview (see WndProc)
			SetWindowPos((HWND)state->guiHwnd, nullptr, 0, 0, (int)width, (int)height,
						 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
			return true;
		}

		bool set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
		{
			if (!window || !window->api || std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0)
				return false;

			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);

			// set_parent() is called once per GUI lifetime; guard against a second call anyway
			if (state->guiHwnd)
			{
				delete state->webUIHost;
				state->webUIHost = nullptr;
				DestroyWindow((HWND)state->guiHwnd);
				state->guiHwnd = nullptr;
			}

			HWND hwnd = CreateWindowExW(0, kWindowClassName, L"", WS_CHILD, 0, 0, (int)kDefaultWidth,
										 (int)kDefaultHeight, (HWND)window->win32, nullptr, thisModuleHandle(), nullptr);
			if (!hwnd)
				return false;

			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
			state->guiHwnd = hwnd;

			try
			{
				state->webUIHost = new WebUIHost(hwnd, state->plugin.get());
			}
			catch (const std::exception& e)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderGui", "Failed to create embedded web UI",
					e.what());
			}
			catch (...)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderGui", "Failed to create embedded web UI",
					"unknown error");
			}

			return true;
		}

		bool set_transient(const clap_plugin_t*, const clap_window_t*)
		{
			return false; // floating not supported
		}

		void suggest_title(const clap_plugin_t*, const char*) {}

		bool gui_show(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			if (!state->guiHwnd)
				return false;
			ShowWindow((HWND)state->guiHwnd, SW_SHOW);
			return true;
		}

		bool gui_hide(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			if (!state->guiHwnd)
				return false;
			ShowWindow((HWND)state->guiHwnd, SW_HIDE);
			return true;
		}
	} // namespace

	const clap_plugin_gui_t reashaderClapGuiExtension = {
		is_api_supported, get_preferred_api, gui_create,  gui_destroy,   set_scale,     get_size,  can_resize,
		get_resize_hints, adjust_size,       set_size,    set_parent,    set_transient, suggest_title,
		gui_show,         gui_hide,
	};
} // namespace ReaShader

#endif // _WIN32
