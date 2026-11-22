// Small compatibility shims needed to link against the prebuilt libraries
// that ship with the repository, plus the cross-platform helpers declared in
// platform.h.

#include "platform.h"
#include "fs_compat.h"

#include <cstdio>
#include <iostream>

#ifdef _WIN32

#include <windows.h>
#include <ctime>

// The bundled libJolt.a was produced by a MinGW toolchain whose libstdc++
// routes std::this_thread::sleep_for through `nanosleep64` (an msvcrt export).
// The MinGW-w64 CRT used here only provides `nanosleep` from winpthreads, so
// provide the missing alias.
extern "C" int nanosleep64(const struct timespec *inRequest, struct timespec *inRemain)
{
    return nanosleep(inRequest, inRemain);
}

#else

#include <unistd.h>
#include <climits>

#endif // _WIN32

namespace Plat {

void ShowMessage(const std::string& title, const std::string& text) {
#ifdef _WIN32
    MessageBoxA(nullptr, text.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
#else
    std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
#endif
}

std::string ExecutablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return FsCompat::toUtf8(std::filesystem::path(buffer));
#else
    char buffer[PATH_MAX + 1]{};
    const ssize_t length = ::readlink("/proc/self/exe", buffer, PATH_MAX);
    if (length <= 0) return {};
    buffer[length] = '\0';
    return std::string(buffer);
#endif
}

} // namespace Plat
