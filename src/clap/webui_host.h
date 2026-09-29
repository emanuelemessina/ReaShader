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

	// The embedded web UI: a webview (WebView2 on Windows) showing ui/index.html inside the FX window,
	// bridged to ReaShaderPlugin through JSON messages.
	//
	// The webview and its message loop live on a background thread owned by this class, because
	// creating a webview blocks for seconds, which REAPER's UI thread doesn't tolerate. Every call
	// from other threads goes through webview::dispatch(), so none of the methods here block.
	class WebUIHost
	{
	  public:
		// parentHwnd: the container HWND (void* keeps windows.h out of this header).
		// plugin: not owned, must outlive this object.
		WebUIHost(void* parentHwnd, ReaShaderPlugin* plugin);
		~WebUIHost(); // blocks until the webview thread has exited

		WebUIHost(const WebUIHost&) = delete;
		WebUIHost& operator=(const WebUIHost&) = delete;

		// fits the webview to the container's client area
		void resize(int width, int height);

	  private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
} // namespace ReaShader

#endif // _WIN32
