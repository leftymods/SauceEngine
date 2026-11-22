// Compatibility shims required to link against the prebuilt libraries that
// ship with the repository.
#ifdef _WIN32

#include <ctime>

// The bundled libJolt.a was produced by a MinGW toolchain whose libstdc++
// routes std::this_thread::sleep_for through `nanosleep64` (an msvcrt export).
// MinGW-w64 only provides `nanosleep` (from winpthreads), so provide the alias.
extern "C" int nanosleep64(const struct timespec *inRequest, struct timespec *inRemain)
{
    return nanosleep(inRequest, inRemain);
}

#endif // _WIN32
