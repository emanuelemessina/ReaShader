/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "shell.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace util::shell
{
	void openUrl(const std::string& url)
	{
		if (!url.starts_with("https://") || url.find_first_of("\"'` ") != std::string::npos)
			return;

#ifdef _WIN32
		int length = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
		std::wstring wideUrl((size_t)length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wideUrl.data(), length);
		ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
		(void)std::system(("open '" + url + "'").c_str()); // TODO: untested on macOS
#else
		(void)std::system(("xdg-open '" + url + "'").c_str()); // TODO: untested on Linux
#endif
	}
} // namespace util::shell
