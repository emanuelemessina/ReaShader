/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#ifdef _WIN32

#include <memory>

namespace ReaShader
{
	class ReaShaderPlugin;

	// Owns the embedded webview::webview instance (github.com/webview/webview, WebView2-backed on
	// Windows) that hosts rsui.html directly inside the FX-window child HWND created by
	// reashader_clap_gui_win32.cpp's set_parent() -- replaces the old RSUIServer/restinio +
	// external-browser-tab design (see CLAUDE.md's Phase D notes). Pimpl'd so webview.h (a large
	// amalgamated header) doesn't leak into every TU that touches ClapPluginState, same reasoning
	// as backend.h's std::any-hiding trick for restinio before it.
	//
	// The webview::webview object, and its entire message loop, live on a dedicated background
	// thread this class owns -- never on REAPER's UI thread. webview::webview's constructor blocks
	// (pumping its own nested message loop) for however long WebView2's async environment/
	// controller setup takes, which can be multiple seconds; REAPER's own message loop doesn't
	// tolerate a plugin blocking/re-entering it for that long (confirmed empirically: REAPER
	// fatally aborted, always at the same internal code location, whenever this ran on the UI
	// thread -- see CLAUDE.md's Phase D notes for the full story). All calls into the webview after
	// construction go through webview::webview::dispatch(), which the library documents as safe to
	// call from any thread and which marshals execution onto whichever thread is actually running
	// the webview's loop (our background thread) -- so the constructor here returns immediately,
	// and resize()/message-sending never block the calling (UI) thread either.
	class WebUIHost
	{
	  public:
		// parentHwnd: the container child HWND created by set_parent(), passed as void* to keep
		// windows.h out of this header. plugin: not owned; must outlive this object (the CLAP gui
		// extension always tears the GUI down, via gui_destroy, before the plugin instance itself).
		// Returns immediately -- the actual webview construction happens on the background thread.
		WebUIHost(void* parentHwnd, ReaShaderPlugin* plugin);
		// blocks until the background thread has fully torn down the webview and exited
		~WebUIHost();

		WebUIHost(const WebUIHost&) = delete;
		WebUIHost& operator=(const WebUIHost&) = delete;

		// resizes the embedded webview to fill the given client area (container-local coordinates)
		// -- called from clap.gui's set_size()/the container WndProc's WM_SIZE handler. Non-blocking:
		// posts the resize to the background thread rather than performing it inline.
		void resize(int width, int height);

	  private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
} // namespace ReaShader

#endif // _WIN32
