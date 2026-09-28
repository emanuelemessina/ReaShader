/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <memory>

#include <clap/clap.h>

#include "reashaderplugin.h"

// Shared between reashader_clap.cpp (the plugin shell: entry/factory/audio-ports/params/state)
// and reashader_clap_gui_win32.cpp (the native GUI extension) -- both need to reach the same
// per-instance ReaShaderPlugin via clap_plugin_t::plugin_data.

#ifdef _WIN32
namespace ReaShader
{
	class WebUIHost; // defined in webui_host_win32.h -- kept out of this widely-included header
}
#endif

namespace ReaShader
{
	struct ClapPluginState
	{
		const clap_host_t* host{ nullptr };
		std::unique_ptr<ReaShaderPlugin> plugin;
#ifdef _WIN32
		void* guiHwnd{ nullptr }; // native child window created by the gui extension, if any -- the
								  // parent the embedded webview is created inside

		// raw pointer, not unique_ptr: WebUIHost is only forward-declared here, and this struct
		// crosses into reashader_clap.cpp (a TU that never needs WebUIHost's full definition) --
		// a unique_ptr member would force that TU to see a complete WebUIHost just to destroy
		// ClapPluginState. Manually new'd/delete'd from reashader_clap_gui_win32.cpp only, same
		// manual-lifetime pattern as guiHwnd above.
		WebUIHost* webUIHost{ nullptr };
#endif
	};

#ifdef _WIN32
	extern const clap_plugin_gui_t reashaderClapGuiExtension;
#endif
} // namespace ReaShader
