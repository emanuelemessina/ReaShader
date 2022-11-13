#pragma once

#include <string>
#include <vector>

#define VST3_BUNDLE_DIR tools::paths::goUp(tools::paths::getDynamicLibraryDir(), 1)
#define RESOURCES_DIR tools::paths::join({VST3_BUNDLE_DIR,"Resources"})

#define GET_RESOURCE_DIR(resource_dirname) tools::paths::join({RESOURCES_DIR, resource_dirname})

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