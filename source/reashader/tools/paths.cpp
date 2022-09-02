#include "paths.h"
#include "strings.h"

#if defined(_WIN32)
#include <windows.h>
#include <Shlwapi.h>
#include <io.h> 

#define access _access_s
#endif

#ifdef __APPLE__
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>

#include <dlfcn.h>

#endif

#ifdef __linux__
#include <limits.h>
#include <libgen.h>
#include <unistd.h>

#include <dlfcn.h>

#if defined(__sun)
#define PROC_SELF_EXE "/proc/self/path/a.out"
#else
#define PROC_SELF_EXE "/proc/self/exe"
#endif

#endif

#include "cwalk.h"

namespace tools {

	namespace paths {

#if defined(_WIN32)

		std::string getExecutablePath() {
			char rawPathName[MAX_PATH];
			GetModuleFileNameA(NULL, rawPathName, MAX_PATH);
			return std::string(rawPathName);
		}

		std::string getExecutableDir() {
			std::string executablePath = getExecutablePath();
			PathRemoveFileSpecA(executablePath.data());
			return executablePath;
		}

		std::string getDynamicLibraryPath() {
			wchar_t path[MAX_PATH];
			HMODULE hm = NULL;

			if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)&getDynamicLibraryPath, &hm) == 0)
			{
				int ret = GetLastError();
				fprintf(stderr, "GetModuleHandle failed, error = %d\n", ret);
				// Return or however you want to handle an error.
			}
			if (GetModuleFileName(hm, path, sizeof(path)) == 0)
			{
				int ret = GetLastError();
				fprintf(stderr, "GetModuleFileName failed, error = %d\n", ret);
				// Return or however you want to handle an error.
			}

			return strings::ws2s(path);
		}

#endif

#ifdef __linux__

		std::string getExecutablePath() {
			char rawPathName[PATH_MAX];
			realpath(PROC_SELF_EXE, rawPathName);
			return  std::string(rawPathName);
		}

		std::string getExecutableDir() {
			std::string executablePath = getExecutablePath();
			char* executableDir = dirname(executablePath.data());
			return std::string(executableDir);
		}

#define _GNU_SOURCE

		std::string getDynamicLibraryPath() {
			Dl_info dl_info;
			dladdr((void*)getDynamicLibraryPath, &dl_info);
			return(dl_info.dli_fname);
		}

#endif

#ifdef __APPLE__

		std::string getExecutablePath() {
			char rawPathName[PATH_MAX];
			char realPathName[PATH_MAX];
			uint32_t rawPathSize = (uint32_t)sizeof(rawPathName);

			if (!_NSGetExecutablePath(rawPathName, &rawPathSize)) {
				realpath(rawPathName, realPathName);
			}
			return  std::string(realPathName);
		}

		std::string getExecutableDir() {
			std::string executablePath = getExecutablePath();
			char* executableDir = dirname(executablePath.data());
			return std::string(executableDir);
		}

		std::string getDynamicLibraryPath() {

			//TODO: try this code

			// List all mach-o images in a process
			/*uint32_t i;
			uint32_t ic = _dyld_image_count();
			printf("Got %d images\n", ic);
			for (i = 0; i < ic; i++)
			{
				printf("%d: %p\t%s\t(slide: %p)\n", i,
					_dyld_get_image_header(i),
					_dyld_get_image_name(i),
					_dyld_get_image_slide(i));
			}*/

			Dl_info dl_info;
			dladdr((void*)getDynamicLibraryPath, &dl_info);
			return(dl_info.dli_fname);
		}

#endif

		bool fileExists(const std::string& filePath) {
			return access(filePath.c_str(), 0) == 0;
		}

		std::string getDynamicLibraryDir() {
			std::string path = getDynamicLibraryPath();
			size_t length = 0;
			cwk_path_get_dirname(path.data(), &length);
			return path.substr(0, length / sizeof(char));
		}

		std::string join(const std::vector<std::string>& paths) {
			// reference to vector -> temporary or at least do not copy
			// const reference -> will not modify the vector

			// const pointer -> will not reassign the pointer
			// to pointer of const char -> can reassign the pointer to const char (set array items)
			// but will not modify any char that it points to
			char const** const ps = new char const* [paths.size() + 1]; // allocate new char* array to store the internal string pointers
			// + 1 to store the NULL path (required by cwalk join multiple)

			size_t size = 0; // total size of joined buffer

			for (int i = 0; i < paths.size(); i++) {
				size += paths[i].size(); // add num chars of current path
				ps[i] = paths[i].data(); // each char const * of ps is exactly the internal char* of the std string
			}

			ps[paths.size()] = NULL; // required by cwalk join multiple

			size += paths.size(); // num paths - 1 = num slashes , +1 null terminator

			char* const joined = new char[size]; // allocate new buffer

			size *= sizeof(char); // get size in bytes

			cwk_path_join_multiple((char const**)ps, joined, size);

			delete[] ps; // free the array

			return joined;
		}

		std::string goUp(const std::string& path, const int levels) {
			std::vector<std::string> ps{};
			ps.assign(levels, "..");
			ps.insert(ps.begin(), path);
			return join(ps);
		}

	}

}

