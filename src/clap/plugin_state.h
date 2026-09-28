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

namespace ReaShader
{
	struct ClapPluginState
	{
		const clap_host_t* host{ nullptr };
		std::unique_ptr<ReaShaderPlugin> plugin;
#ifdef _WIN32
		void* guiHwnd{ nullptr }; // native child window created by the gui extension, if any
#endif
	};

#ifdef _WIN32
	extern const clap_plugin_gui_t reashaderClapGuiExtension;
#endif
} // namespace ReaShader
