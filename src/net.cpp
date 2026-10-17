// Multiplayer networking (DirectPlay 3, TCP/IP).
//
// One DirectPlay. The game creates DirectPlay through COM (the system's dplayx.dll,
// main.cpp), but it also imports DPLAYX.dll directly (DirectPlayLobbyCreateA,
// ordinal 4, slot 0x595618, called at 0x4c6ca9 to build the TCP/IP address), and
// that import binds to GOG's "DirectPlay Stub" in the game folder. The TCP/IP
// service provider, dpwsockx.dll, imports DPLAYX.dll!gdwDPlaySPRefCount, which the
// loader can bind to that stub too (the first module named dplayx.dll), so the
// real DirectPlay never counts the provider. Before DirectPlay is created, both are
// pointed at the system's dplayx.dll.
//
// NAT. Every message dpwsockx sends starts with a header: a dword (size in the low
// 20 bits, a token 0xFAB/0xCAB/0xBAB in the high 12) and the sender's reply
// address (SOCKADDR_IN), which the receiver answers to. Behind a router that is a
// private address the other side can't reach. On receipt, a private (or zero)
// reply address from a public source is replaced by the address the message
// actually came from. Also: [Debug] NetLog=1 logs every bind, connect, accept and
// message header; [Network] UPnP=1 asks the router to forward the ports
// DirectPlay listens on (upnp.cpp).
#include "net.h"

#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <mutex>
#include <string>

#include "diag.h"
#include "upnp.h"

namespace net {
namespace {

void (*g_log)(const char*, ...);
bool g_netLog, g_natFix = true, g_upnp = true;

constexpr uintptr_t kLobbyCreateSlot = 0x595618;  // WARBREED.EXE's DPLAYX import (#4)
constexpr uint32_t kTokenMask = 0xFFF00000, kSizeMask = 0x000FFFFF;

decltype(&recv) o_recv;
decltype(&recvfrom) o_recvfrom;
decltype(&send) o_send;
decltype(&sendto) o_sendto;
decltype(&connect) o_connect;
decltype(&accept) o_accept;
decltype(&bind) o_bind;

std::string Addr(const sockaddr* a) {
    if (!a || a->sa_family != AF_INET) return "(not IPv4)";
    const sockaddr_in* s = (const sockaddr_in*)a;
    const uint8_t* b = (const uint8_t*)&s->sin_addr;
    char t[32];
    snprintf(t, sizeof t, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], ntohs(s->sin_port));
    return t;
}

// Addresses a remote player can't reach: unset, private, shared (CGNAT), link-local, loopback.
bool Unreachable(const in_addr& a) {
    const uint8_t* b = (const uint8_t*)&a;
    return a.s_addr == 0 || b[0] == 10 || b[0] == 127 || (b[0] == 172 && (b[1] & 0xf0) == 16) ||
           (b[0] == 192 && b[1] == 168) || (b[0] == 169 && b[1] == 254) || (b[0] == 100 && (b[1] & 0xc0) == 64);
}

bool IsHeader(const char* buf, int n) {
    if (n < 20) return false;
    uint32_t t = *(const uint32_t*)buf & kTokenMask;
    return t == 0xFAB00000 || t == 0xCAB00000 || t == 0xBAB00000;
}

// One message header received from `peer`: logged, and its reply address made
// reachable when the sender is behind a router.
void OnHeader(char* buf, const sockaddr_in* peer, const char* how) {
    uint32_t mixed = *(uint32_t*)buf;
    sockaddr_in* reply = (sockaddr_in*)(buf + 4);
    if (g_netLog)
        g_log("net: %s %s: token %03x size %u reply %s", how, Addr((const sockaddr*)peer).c_str(), mixed >> 20,
              mixed & kSizeMask, Addr((const sockaddr*)reply).c_str());
    if (!g_natFix || !peer || peer->sin_family != AF_INET || reply->sin_family != AF_INET) return;
    if (Unreachable(peer->sin_addr) || !Unreachable(reply->sin_addr)) return;  // a LAN, or already reachable
    if (reply->sin_addr.s_addr == peer->sin_addr.s_addr) return;
    std::string was = Addr((const sockaddr*)reply);
    reply->sin_addr = peer->sin_addr;
    g_log("net: reply address %s from %s -> %s (the sender is behind a router)", was.c_str(),
          Addr((const sockaddr*)peer).c_str(), Addr((const sockaddr*)reply).c_str());
}

// TCP streams: where the next header starts, per socket.
struct Stream {
    uint32_t left = 0;  // bytes of the current message still to come
    sockaddr_in peer{};
};
std::mutex g_mu;
std::map<SOCKET, Stream> g_streams;

int WSAAPI h_recv(SOCKET s, char* buf, int len, int flags) {
    int r = o_recv(s, buf, len, flags);
    if (r <= 0 || (flags & MSG_PEEK)) return r;
    std::lock_guard<std::mutex> lock(g_mu);
    Stream& st = g_streams[s];
    if (st.peer.sin_family != AF_INET) {
        int n = sizeof st.peer;
        getpeername(s, (sockaddr*)&st.peer, &n);
    }
    for (int pos = 0; pos < r;) {
        if (st.left == 0) {
            if (!IsHeader(buf + pos, r - pos)) break;  // not at a header we can read whole: leave it
            OnHeader(buf + pos, &st.peer, "tcp from");
            st.left = *(uint32_t*)(buf + pos) & kSizeMask;
            if (st.left == 0) break;
        }
        uint32_t take = std::min<uint32_t>(st.left, (uint32_t)(r - pos));
        st.left -= take;
        pos += (int)take;
    }
    return r;
}

int WSAAPI h_recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen) {
    sockaddr_in own{};
    int ownLen = sizeof own;
    if (!from) from = (sockaddr*)&own, fromlen = &ownLen;
    int r = o_recvfrom(s, buf, len, flags, from, fromlen);
    if (r > 0 && !(flags & MSG_PEEK) && IsHeader(buf, r)) OnHeader(buf, (const sockaddr_in*)from, "udp from");
    return r;
}

int WSAAPI h_send(SOCKET s, const char* buf, int len, int flags) {
    if (g_netLog && IsHeader(buf, len)) {
        sockaddr_in peer{};
        int n = sizeof peer;
        getpeername(s, (sockaddr*)&peer, &n);
        g_log("net: tcp to %s: token %03x size %u reply %s", Addr((const sockaddr*)&peer).c_str(),
              *(const uint32_t*)buf >> 20, *(const uint32_t*)buf & kSizeMask, Addr((const sockaddr*)(buf + 4)).c_str());
    }
    return o_send(s, buf, len, flags);
}

int WSAAPI h_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen) {
    if (g_netLog && IsHeader(buf, len))
        g_log("net: udp to %s: token %03x size %u reply %s", Addr(to).c_str(), *(const uint32_t*)buf >> 20,
              *(const uint32_t*)buf & kSizeMask, Addr((const sockaddr*)(buf + 4)).c_str());
    return o_sendto(s, buf, len, flags, to, tolen);
}

int WSAAPI h_connect(SOCKET s, const sockaddr* to, int tolen) {
    int r = o_connect(s, to, tolen);
    int err = r ? WSAGetLastError() : 0;
    if (g_netLog || (r && err != WSAEWOULDBLOCK)) g_log("net: connect to %s -> %d (%d)", Addr(to).c_str(), r, err);
    if (r) WSASetLastError(err);
    return r;
}

SOCKET WSAAPI h_accept(SOCKET s, sockaddr* from, int* fromlen) {
    sockaddr_in own{};
    int ownLen = sizeof own;
    if (!from) from = (sockaddr*)&own, fromlen = &ownLen;
    SOCKET c = o_accept(s, from, fromlen);
    if (c != INVALID_SOCKET) {
        if (g_netLog) g_log("net: accepted %s", Addr(from).c_str());
        std::lock_guard<std::mutex> lock(g_mu);
        Stream& st = g_streams[c];
        st = Stream{};
        if (from->sa_family == AF_INET) st.peer = *(sockaddr_in*)from;
    }
    return c;
}

int WSAAPI h_bind(SOCKET s, const sockaddr* at, int len) {
    int r = o_bind(s, at, len);
    if (r == 0) {
        sockaddr_in got{};
        int n = sizeof got;
        getsockname(s, (sockaddr*)&got, &n);
        int type = 0, tn = sizeof type;
        getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&type, &tn);
        bool tcp = type == SOCK_STREAM;
        g_log("net: DirectPlay listens on %s port %u", tcp ? "TCP" : "UDP", ntohs(got.sin_port));
        if (g_upnp && got.sin_port) upnp::Map(ntohs(got.sin_port), tcp);
    }
    return r;
}

// Rebinds a module's imports from one DLL: each entry whose current target is
// `match(old)` gets `replace(old)`. Returns how many changed.
template <class F>
int PatchImports(HMODULE m, const char* dll, F swap) {
    auto base = (uint8_t*)m;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int changed = 0;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; slots->u1.Function; ++slots, ++names) {
            const char* name = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                                   ? (const char*)(uintptr_t)IMAGE_ORDINAL(names->u1.Ordinal)
                                   : (const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name;
            uintptr_t now = slots->u1.Function, want = swap(name, now);
            if (!want || want == now) continue;
            DWORD old;
            if (VirtualProtect(&slots->u1.Function, sizeof(uintptr_t), PAGE_READWRITE, &old)) {
                slots->u1.Function = want;
                VirtualProtect(&slots->u1.Function, sizeof(uintptr_t), old, &old);
                ++changed;
            }
        }
    }
    return changed;
}

std::string ModuleOf(uintptr_t a) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &m) && m)
        GetModuleFileNameA(m, path, sizeof path);
    return path;
}

void LogDirectPlayModules(const char* when) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 me{};
    me.dwSize = sizeof me;
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me))
        if (_stricmp(me.szModule, "dplayx.dll") == 0 || _stricmp(me.szModule, "dpwsockx.dll") == 0)
            g_log("multiplayer: %s: %s at %p", when, me.szExePath, (void*)me.modBaseAddr);
    CloseHandle(snap);
}

}  // namespace

void Install(void (*log)(const char*, ...), bool netLog, bool natFix, bool upnpOn) {
    g_log = log;
    g_netLog = netLog;
    g_natFix = natFix;
    g_upnp = upnpOn;
    upnp::Init(log);
}

void BeforeDirectPlay() {
    static bool done;
    if (done) return;
    done = true;
    diag::InstallCrashLog();  // again: DirectPlay or the game may have set their own filter
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);  // a 32-bit process on 64-bit Windows gets SysWOW64 through redirection
    HMODULE game = GetModuleHandleA("dplayx.dll");  // the game's own import: GOG's stub, if it is in the game folder
    HMODULE real = LoadLibraryA((std::string(sys) + "\\dplayx.dll").c_str());
    if (!real) {
        g_log("multiplayer: Windows' DirectPlay is not installed (Control Panel > Programs > Turn Windows features "
              "on or off > Legacy Components > DirectPlay)");
        return;
    }
    if (game && game != real) {
        char path[MAX_PATH] = "";
        GetModuleFileNameA(game, path, sizeof path);
        g_log("multiplayer: the game's own DPLAYX import is %s; using Windows' DirectPlay for it", path);
        FARPROC create = GetProcAddress(real, "DirectPlayLobbyCreateA");
        DWORD old;
        if (create && VirtualProtect((void*)kLobbyCreateSlot, 4, PAGE_READWRITE, &old)) {
            *(FARPROC*)kLobbyCreateSlot = create;
            VirtualProtect((void*)kLobbyCreateSlot, 4, old, &old);
        }
    }
    // The TCP/IP service provider, loaded now so its imports can be set before use:
    // DPLAYX (its provider count) to the real DirectPlay, WS2_32 through our hooks.
    HMODULE sp = LoadLibraryA((std::string(sys) + "\\dpwsockx.dll").c_str());
    if (!sp) {
        g_log("multiplayer: dpwsockx.dll (DirectPlay's TCP/IP) not found");
        return;
    }
    int rebound = PatchImports(sp, "DPLAYX.dll", [&](const char* name, uintptr_t now) -> uintptr_t {
        uintptr_t want = (uintptr_t)GetProcAddress(real, name);
        if (want && want != now) g_log("multiplayer: dpwsockx's %s was in %s", (uintptr_t)name > 0xffff ? name : "#",
                                       ModuleOf(now).c_str());
        return want;
    });
    HMODULE ws = GetModuleHandleA("ws2_32.dll");
    struct Hook {
        const char* name;
        void* hook;
        void** orig;
    } hooks[] = {{"recv", (void*)&h_recv, (void**)&o_recv},           {"recvfrom", (void*)&h_recvfrom, (void**)&o_recvfrom},
                 {"send", (void*)&h_send, (void**)&o_send},           {"sendto", (void*)&h_sendto, (void**)&o_sendto},
                 {"connect", (void*)&h_connect, (void**)&o_connect}, {"accept", (void*)&h_accept, (void**)&o_accept},
                 {"bind", (void*)&h_bind, (void**)&o_bind}};
    int hooked = 0;
    for (Hook& h : hooks) {
        uintptr_t target = (uintptr_t)GetProcAddress(ws, h.name);
        *h.orig = (void*)target;
        hooked += PatchImports(sp, "WS2_32.dll", [&](const char*, uintptr_t now) -> uintptr_t {
            return now == target ? (uintptr_t)h.hook : 0;
        });
    }
    g_log("multiplayer: DirectPlay's TCP/IP: %d provider imports set to Windows' DirectPlay, %d socket calls watched "
          "(NAT fix %s, UPnP %s)",
          rebound, hooked, g_natFix ? "on" : "off", g_upnp ? "on" : "off");
    // The session enumeration port (DirectPlay's helper, dplaysvr.exe, listens there for the host).
    if (g_upnp) {
        upnp::Map(47624, true);
        upnp::Map(47624, false);
    }
}

void AfterDirectPlay() { LogDirectPlayModules("loaded"); }

void Shutdown() { upnp::UnmapAll(); }

}  // namespace net
