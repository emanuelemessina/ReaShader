/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "paths.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tools::paths
{
	namespace
	{
		// Full path of the module (the .clap) containing this function
		std::filesystem::path pluginBinaryPath()
		{
#ifdef _WIN32
			HMODULE module = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							   reinterpret_cast<LPCWSTR>(&pluginBinaryPath), &module);

			// grow the buffer until the whole path fits (no MAX_PATH limit)
			std::wstring path(MAX_PATH, L'\0');
			while (true)
			{
				DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
				if (length < path.size())
				{
					path.resize(length);
					return path;
				}
				path.resize(path.size() * 2);
			}
#else
			Dl_info info{};
			dladdr(reinterpret_cast<void*>(&pluginBinaryPath), &info);
			return info.dli_fname;
#endif
		}
	} // namespace

	const std::filesystem::path& pluginDir()
	{
		static const std::filesystem::path dir = pluginBinaryPath().parent_path();
		return dir;
	}
} // namespace tools::paths
