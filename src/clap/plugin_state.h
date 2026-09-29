/**
 * @file
 * @brief ClapPluginState: what one plugin instance is made of (the CLAP plugin, ReaShaderPlugin, the GUI).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <memory>

#include <clap/clap.h>

#include "plugin/plugin.h"

namespace ReaShader
{
	// The plugin window (clap.gui), defined by the platform's GUI implementation
	struct Gui;
	struct GuiDeleter
	{
		void operator()(Gui* gui) const;
	};

	// One per plugin instance, behind clap_plugin_t::plugin_data: shared by the plugin shell and the GUI
	struct ClapPluginState
	{
		clap_plugin_t clapPlugin{}; // what the host sees; plugin_data points back here
		const clap_host_t* host{ nullptr };
		std::unique_ptr<ReaShaderPlugin> plugin;
		std::unique_ptr<Gui, GuiDeleter> gui;
	};

#ifdef _WIN32
	extern const clap_plugin_gui_t guiExtension;
#else
	// TODO: clap.gui on macOS/Linux
	inline void GuiDeleter::operator()(Gui*) const {}
#endif
} // namespace ReaShader
