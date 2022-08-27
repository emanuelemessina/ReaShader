#pragma once

#include <string>
#include <vector>

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