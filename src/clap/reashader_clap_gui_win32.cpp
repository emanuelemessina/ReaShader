/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// Minimal native clap.gui implementation (embedded, Win32 only) -- replaces the old VSTGUI
// panel + system("start ...") combo with a plain child window and a button that shell-opens
// the default browser at RSUIServer's URL. The real UI still lives entirely in that external
// browser tab; this is just the tiny host-embedded launcher, same role the old VST3Editor panel
// played. macOS/Linux are not implemented yet (matches the project's Windows-first precedent).
//
// The window is created lazily, in set_parent() rather than create() -- a WS_CHILD window must
// be created WITH its real parent HWND already known (CreateWindowExW rejects WS_CHILD combined
// with a null hWndParent, failing with ERROR_TLW_WITH_WSCHILD/1406, "top-level window with
// WS_CHILD style" -- easy to misread as some obscure class-registration/environment problem,
// since the error text doesn't obviously map to "you passed the wrong hWndParent"). create()
// only registers the window class and validates the requested API/mode; set_parent() is where
// CreateWindowExW actually runs, with the host-provided HWND passed in directly.

#ifdef _WIN32

#include <cstring>
#include <string>

#include <windows.h>
#include <shellapi.h>

#include "plugin_state.h"

namespace ReaShader
{
	namespace
	{
		constexpr wchar_t kWindowClassName[] = L"ReaShaderGuiWindow";
		constexpr int kButtonId = 1001;
		constexpr uint32_t kWidth = 320;
		constexpr uint32_t kHeight = 90;

		LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			if (msg == WM_COMMAND && LOWORD(wParam) == kButtonId)
			{
				auto* state = reinterpret_cast<ClapPluginState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
				if (state && state->plugin)
				{
					std::string url = state->plugin->getWebUIUrl();
					if (!url.empty())
					{
						// url is always plain ASCII ("http://localhost:<port>")
						std::wstring wurl(url.begin(), url.end());
						ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
				}
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
			*width = kWidth;
			*height = kHeight;
			return true;
		}

		bool can_resize(const clap_plugin_t*)
		{
			return false;
		}

		bool get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
		{
			*hints = {};
			return false;
		}

		bool adjust_size(const clap_plugin_t*, uint32_t* width, uint32_t* height)
		{
			*width = kWidth;
			*height = kHeight;
			return true;
		}

		bool set_size(const clap_plugin_t*, uint32_t, uint32_t)
		{
			return false; // fixed size (can_resize is false)
		}

		bool set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
		{
			if (!window || !window->api || std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0)
				return false;

			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);

			// (re-)parenting an existing window isn't needed in practice (create() -> set_parent()
			// happens once per GUI lifetime per the CLAP spec's documented call order), but guard
			// against being called twice without an intervening destroy() anyway
			if (state->guiHwnd)
			{
				DestroyWindow((HWND)state->guiHwnd);
				state->guiHwnd = nullptr;
			}

			HWND parentHwnd = (HWND)window->win32;

			HWND hwnd = CreateWindowExW(0, kWindowClassName, L"", WS_CHILD, 0, 0, (int)kWidth, (int)kHeight,
										 parentHwnd, nullptr, thisModuleHandle(), nullptr);
			if (!hwnd)
				return false;

			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);

			CreateWindowExW(0, L"BUTTON", L"Open Web UI", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 10, 200, 30, hwnd,
							 (HMENU)(INT_PTR)kButtonId, thisModuleHandle(), nullptr);

			state->guiHwnd = hwnd;
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
