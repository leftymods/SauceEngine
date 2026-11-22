#pragma once

// Small socket portability layer so that multiplayer.cpp can stay mostly
// platform agnostic. On Windows the usual Winsock headers are used, on POSIX
// systems BSD sockets are mapped onto the Winsock style names.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define MP_SELECT_NFDS(s) 0
#define MP_SD_BOTH SD_BOTH

inline int mpSetSocketNonBlocking(SOCKET s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode);
}

inline int mpGetSockName(SOCKET s, sockaddr* addr, int* length) { return getsockname(s, addr, length); }
inline int mpGetPeerName(SOCKET s, sockaddr* addr, int* length) { return getpeername(s, addr, length); }
inline int mpGetSockOpt(SOCKET s, int level, int option, void* value, int* length) {
    return getsockopt(s, level, option, reinterpret_cast<char*>(value), length);
}

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <pwd.h>

using SOCKET = int;
using BOOL = int;
using DWORD = unsigned long;
using u_long = unsigned long;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#define MP_SELECT_NFDS(s) ((s) + 1)
#define MP_SD_BOTH SHUT_RDWR
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define closesocket ::close
#define WSAGetLastError() (errno)
#define InetPtonA(family, src, dst) ::inet_pton(family, src, dst)
#define InetNtopA(family, src, dst, size) ::inet_ntop(family, src, dst, size)
#define GetUserNameA mpGetUserName

// Winsock error codes mapped onto their errno counterparts.
#define WSAEACCES EACCES
#define WSAEADDRINUSE EADDRINUSE
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#define WSAEALREADY EALREADY
#define WSAECONNABORTED ECONNABORTED
#define WSAECONNREFUSED ECONNREFUSED
#define WSAECONNRESET ECONNRESET
#define WSAEDESTADDRREQ EDESTADDRREQ
#define WSAEFAULT EFAULT
#define WSAEHOSTUNREACH EHOSTUNREACH
#define WSAEINPROGRESS EINPROGRESS
#define WSAEINTR EINTR
#define WSAEINVAL EINVAL
#define WSAEISCONN EISCONN
#define WSAEMFILE EMFILE
#define WSAEMSGSIZE EMSGSIZE
#define WSAENETDOWN ENETDOWN
#define WSAENETUNREACH ENETUNREACH
#define WSAENOTCONN ENOTCONN
#define WSAENOTSOCK ENOTSOCK
#define WSAETIMEDOUT ETIMEDOUT
#define WSAEWOULDBLOCK EWOULDBLOCK

inline int mpSetSocketNonBlocking(SOCKET s) {
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) return SOCKET_ERROR;
    return ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

inline int mpGetSockName(SOCKET s, sockaddr* addr, int* length) {
    socklen_t len = length ? static_cast<socklen_t>(*length) : 0;
    const int rc = ::getsockname(s, addr, &len);
    if (length) *length = static_cast<int>(len);
    return rc;
}

inline int mpGetPeerName(SOCKET s, sockaddr* addr, int* length) {
    socklen_t len = length ? static_cast<socklen_t>(*length) : 0;
    const int rc = ::getpeername(s, addr, &len);
    if (length) *length = static_cast<int>(len);
    return rc;
}

inline int mpGetSockOpt(SOCKET s, int level, int option, void* value, int* length) {
    socklen_t len = length ? static_cast<socklen_t>(*length) : 0;
    const int rc = ::getsockopt(s, level, option, value, &len);
    if (length) *length = static_cast<int>(len);
    return rc;
}

inline bool mpGetUserName(char* buffer, DWORD* size) {
    if (!buffer || !size) return false;
    const passwd* pw = ::getpwuid(::getuid());
    const char* name = (pw && pw->pw_name && *pw->pw_name) ? pw->pw_name : "";
    std::snprintf(buffer, static_cast<size_t>(*size), "%s", name);
    *size = static_cast<DWORD>(std::strlen(buffer) + 1);
    return name[0] != '\0';
}

#endif // _WIN32
