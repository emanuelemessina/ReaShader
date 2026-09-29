/**
 * @file
 * @brief Opening URLs in the system browser.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <string>

namespace util::shell
{
	// Opens an https:// URL in the system's browser; other URLs are ignored
	void openUrl(const std::string& url);
} // namespace util::shell
