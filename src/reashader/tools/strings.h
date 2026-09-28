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
#include <codecvt>

#include <locale>

namespace tools
{
	namespace strings
	{

		inline std::u16string bytes_to_u16string(const std::vector<std::uint8_t>& bytes)
		{
			std::u16string result;

			for (std::size_t i = 0; i < bytes.size(); i += 2)
			{
				char16_t combinedChar = (bytes[i] << 8) | bytes[i + 1];
				result.push_back(combinedChar);
			}

			return result;
		}

		// ------------------------------------------

		// lossy conversions

		inline std::wstring s2ws(const std::string& str)
		{
			using convert_typeX = std::codecvt_utf8<wchar_t>;
			std::wstring_convert<convert_typeX, wchar_t> converterX;

			return converterX.from_bytes(str);
		}

		inline std::string ws2s(const std::wstring& wstr)
		{
			using convert_typeX = std::codecvt_utf8<wchar_t>;
			std::wstring_convert<convert_typeX, wchar_t> converterX;

			return converterX.to_bytes(wstr);
		}

		inline std::string u16string_to_string(const std::u16string& u16str)
		{
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
			return converter.to_bytes(u16str);
		}

		inline std::u16string string_to_u16string(const std::string& str)
		{
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
			return converter.from_bytes(str);
		}

		// ---------------------------------------------

		/**
		 * Copies str to a new writable char*.
		 *
		 * \param str
		 * \return
		 */
		inline char* cp_to_cptr(const std::string str)
		{
			char* writable = new char[str.size() + 1];
			std::copy(str.begin(), str.end(), writable);
			writable[str.size()] = '\0'; // don't forget the terminating 0
			// don't forget to free the string after finishing using it
			return writable;
		}

		inline std::string utf16_to_utf8(char16_t* utf16)
		{
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
			return convert.to_bytes(utf16);
		}
		inline std::u16string utf8_to_utf16(char* utf8)
		{
			std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
			return convert.from_bytes(utf8);
		}
	} // namespace strings
} // namespace tools
