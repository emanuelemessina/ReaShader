/**
 * @file
 * @brief Paths to the plugin's folder and its resources.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <filesystem>

// Resources are deployed next to the plugin binary:
// <plugin dir>/ReaShader.clap
// <plugin dir>/resources/...
// <plugin dir>/ui/...

namespace util::paths
{
	const std::filesystem::path& pluginDir();

	inline std::filesystem::path resourcesDir()
	{
		return pluginDir() / "resources";
	}

	inline std::filesystem::path uiDir()
	{
		return pluginDir() / "ui";
	}

	// shaders compiled on upload, as <name>.json
	inline std::filesystem::path compiledShadersDir()
	{
		return resourcesDir() / "shaders" / "compiled";
	}

	// LUTs parsed on upload, as <name>.json
	inline std::filesystem::path lutsDir()
	{
		return resourcesDir() / "luts";
	}
} // namespace util::paths
