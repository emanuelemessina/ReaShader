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

#ifdef _WIN32
namespace ReaShader
{
	class WebUIHost;
}
#endif

namespace ReaShader
{
	// Per-instance data behind clap_plugin_t::plugin_data, shared by the plugin shell and the GUI extension.
	struct ClapPluginState
	{
		const clap_host_t* host{ nullptr };
		std::unique_ptr<ReaShaderPlugin> plugin;
#ifdef _WIN32
		void* guiHwnd{ nullptr };			 // container child window, parent of the embedded webview
		WebUIHost* webUIHost{ nullptr }; // owned; created and deleted by reashader_clap_gui_win32.cpp
#endif
	};

#ifdef _WIN32
	extern const clap_plugin_gui_t reashaderClapGuiExtension;
#endif
} // namespace ReaShader
