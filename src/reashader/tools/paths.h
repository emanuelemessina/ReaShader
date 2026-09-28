/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <string>
#include <vector>

// CLAP plugins are a single flat file (no VST3-style bundle folder) -- resources are staged
// directly next to the built .clap by CMakeLists.txt's POST_BUILD step, not under a "Resources"
// subfolder one level up like the old VST3 layout assumed.
#define REASHADER_PLUGIN_DIR tools::paths::getDynamicLibraryDir()

#define GET_RESOURCE_DIR(resource_dirname) tools::paths::join({REASHADER_PLUGIN_DIR, resource_dirname})

#define ASSETS_DIR GET_RESOURCE_DIR("assets")
#define RSUI_DIR GET_RESOURCE_DIR("rsui")

namespace tools {

	namespace paths {

		std::string getExecutablePath();
		std::string getExecutableDir();
		bool fileExists(const std::string& filePath);
		std::string getDynamicLibraryPath();
		std::string getDynamicLibraryDir();
		std::string join(const std::vector<std::string>& paths);
		std::string goUp(const std::string& path, const int levels);

	}

}