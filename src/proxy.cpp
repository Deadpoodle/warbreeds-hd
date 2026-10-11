// wsock32.dll proxy. WARBREED.EXE statically imports WSOCK32.dll, which is not
// a KnownDLL, so this DLL in the game folder is loaded at process start and
// gives the patch a place to run without modifying the exe.
//
// Almost every wsock32 export is a forwarder to ws2_32/mswsock and is forwarded
// the same way in wsock32.def. The four below are implemented by the real
// wsock32.dll itself, so they are routed to it.
#include <winsock.h>
#include <windows.h>

static HMODULE real_wsock32;

static FARPROC real(const char* name) {
    if (!real_wsock32) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        lstrcpyA(path + n, "\\wsock32.dll");
        real_wsock32 = LoadLibraryA(path);
    }
    return real_wsock32 ? GetProcAddress(real_wsock32, name) : nullptr;
}

#define FORWARD(name, ret, params, args)                              \
    extern "C" ret WINAPI proxy_##name params {                       \
        typedef ret(WINAPI * fn_t) params;                            \
        static fn_t fn = (fn_t)real(#name);                           \
        return fn args;                                               \
    }

FORWARD(getsockopt, int, (SOCKET s, int level, int opt, char* val, int* len), (s, level, opt, val, len))
FORWARD(setsockopt, int, (SOCKET s, int level, int opt, const char* val, int len), (s, level, opt, val, len))
FORWARD(recv, int, (SOCKET s, char* buf, int len, int flags), (s, buf, len, flags))
FORWARD(recvfrom, int, (SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen),
        (s, buf, len, flags, from, fromlen))
