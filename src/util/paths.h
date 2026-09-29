/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <filesystem>

// Resources are deployed next to the plugin binary:
// <plugin dir>/ReaShader.clap
// <plugin dir>/assets/...
// <plugin dir>/rsui/...

namespace tools::paths
{
	const std::filesystem::path& pluginDir();

	inline std::filesystem::path assetsDir()
	{
		return pluginDir() / "assets";
	}

	inline std::filesystem::path rsuiDir()
	{
		return pluginDir() / "rsui";
	}
} // namespace tools::paths
