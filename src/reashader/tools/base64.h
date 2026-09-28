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

namespace tools {

	namespace base64 {

		std::vector<char> decode(const std::string& encoded);

	}

}
