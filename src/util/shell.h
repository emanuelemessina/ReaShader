/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <string>

namespace util::shell
{
	// Opens an https:// URL in the system's browser; other URLs are ignored
	void openUrl(const std::string& url);
} // namespace util::shell
