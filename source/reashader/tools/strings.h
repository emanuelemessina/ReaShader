#pragma once

#include <string>

#include <codecvt>

#include <locale>

namespace tools {
	namespace strings {

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

		/**
		 * Copies str to a new writable char*.
		 *
		 * \param str
		 * \return
		 */
		inline char* s2cptr(const std::string str) {
			char* writable = new char[str.size() + 1];
			std::copy(str.begin(), str.end(), writable);
			writable[str.size()] = '\0'; // don't forget the terminating 0
			// don't forget to free the string after finished using it
			return writable;
		}

	}
}
