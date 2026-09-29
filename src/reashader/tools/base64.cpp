/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "base64.h"

#include <array>
#include <cstdint>

namespace tools {

	namespace base64 {

		namespace {
			constexpr std::array<int8_t, 256> buildDecodeTable()
			{
				std::array<int8_t, 256> table{};
				table.fill(-1);
				const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				for (int i = 0; i < 64; i++)
					table[(unsigned char)alphabet[i]] = (int8_t)i;
				return table;
			}
		}

		std::vector<char> decode(const std::string& encoded)
		{
			static const std::array<int8_t, 256> decodeTable = buildDecodeTable();

			std::vector<char> out;
			out.reserve((encoded.size() / 4) * 3);

			uint32_t buffer = 0;
			int bits = 0;

			for (char ch : encoded)
			{
				auto c = static_cast<unsigned char>(ch);
				if (c == '=' || c == '\n' || c == '\r')
					continue;

				int8_t value = decodeTable[c];
				if (value < 0)
					continue; // skip characters outside the alphabet

				buffer = (buffer << 6) | (uint32_t)value;
				bits += 6;

				if (bits >= 8)
				{
					bits -= 8;
					out.push_back((char)((buffer >> bits) & 0xFF));
				}
			}

			return out;
		}

	}

}
