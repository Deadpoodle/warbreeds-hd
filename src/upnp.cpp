// UPnP port forwarding: asks the router (an Internet Gateway Device) to forward
// the ports DirectPlay listens on to this PC, so players behind a router can be
// reached without setting up port forwarding by hand. On a background thread:
// SSDP discovery (UDP multicast 239.255.255.250:1900), the router's description
// (HTTP), then SOAP AddPortMapping on its WANIPConnection or WANPPPConnection
// service. The mappings are removed at exit (UnmapAll). Routers with UPnP turned
// off, or connections behind the provider's own NAT (CGNAT), can't be helped:
// the log says which.
#include "upnp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace upnp {
namespace {

void (*g_log)(const char*, ...);

struct Mapping {
    int port;
    bool tcp;
};
std::mutex g_mu;
std::condition_variable g_cv;
std::vector<Mapping> g_queue, g_done;
bool g_started, g_failed;
std::string g_host, g_control, g_service, g_localIp;  // the router's control endpoint
int g_port;

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// host:port/path from an http:// URL.
bool SplitUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    if (Lower(url.substr(0, 7)) != "http://") return false;
    size_t slash = url.find('/', 7);
    std::string hp = url.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    size_t colon = hp.find(':');
    host = hp.substr(0, colon);
    port = colon == std::string::npos ? 80 : atoi(hp.c_str() + colon + 1);
    return true;
}

// One HTTP/1.0 request (so no chunked reply); returns the body, or "" on failure.
std::string Http(const std::string& host, int port, const std::string& request, std::string* localIp = nullptr) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return "";
    DWORD ms = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof ms);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons((u_short)port);
    to.sin_addr.s_addr = inet_addr(host.c_str());
    std::string reply;
    if (connect(s, (sockaddr*)&to, sizeof to) == 0) {
        if (localIp) {
            sockaddr_in me{};
            int n = sizeof me;
            getsockname(s, (sockaddr*)&me, &n);
            *localIp = inet_ntoa(me.sin_addr);
        }
        send(s, request.data(), (int)request.size(), 0);
        char buf[4096];
        int n;
        while ((n = recv(s, buf, sizeof buf, 0)) > 0) reply.append(buf, n);
    }
    closesocket(s);
    size_t body = reply.find("\r\n\r\n");
    if (reply.compare(0, 12, "HTTP/1.1 200") != 0 && reply.compare(0, 12, "HTTP/1.0 200") != 0) {
        if (!reply.empty()) g_log("upnp: %s", reply.substr(0, reply.find("\r\n")).c_str());
        return "";
    }
    return body == std::string::npos ? "" : reply.substr(body + 4);
}

std::string Tag(const std::string& xml, const std::string& tag, size_t from = 0) {
    size_t a = Lower(xml).find("<" + Lower(tag) + ">", from);
    if (a == std::string::npos) return "";
    a += tag.size() + 2;
    size_t b = Lower(xml).find("</" + Lower(tag) + ">", a);
    return b == std::string::npos ? "" : xml.substr(a, b - a);
}

std::string Soap(const std::string& action, const std::string& args) {
    std::string body = "<?xml version=\"1.0\"?>\r\n"
                       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                       "<u:" + action + " xmlns:u=\"" + g_service + "\">" + args + "</u:" + action +
                       "></s:Body></s:Envelope>\r\n";
    char head[512];
    snprintf(head, sizeof head,
             "POST %s HTTP/1.0\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
             "SOAPAction: \"%s#%s\"\r\nContent-Length: %u\r\n\r\n",
             g_control.c_str(), g_host.c_str(), g_port, g_service.c_str(), action.c_str(), (unsigned)body.size());
    return Http(g_host, g_port, head + body);
}

// Finds the router and its WAN connection service. False: none answered.
bool Discover() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    DWORD ms = 2500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    to.sin_addr.s_addr = inet_addr("239.255.255.250");
    const char* targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                             "urn:schemas-upnp-org:service:WANIPConnection:1"};
    for (const char* st : targets) {
        char req[256];
        snprintf(req, sizeof req,
                 "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", st);
        sendto(s, req, (int)strlen(req), 0, (sockaddr*)&to, sizeof to);
    }
    std::string location;
    char buf[2048];
    int n;
    while (location.empty() && (n = recvfrom(s, buf, sizeof buf - 1, 0, nullptr, nullptr)) > 0) {
        buf[n] = 0;
        std::string r = buf, lr = Lower(r);
        size_t at = lr.find("\nlocation:");
        if (at == std::string::npos) continue;
        size_t a = r.find_first_not_of(" \t", at + 10), b = r.find("\r\n", a);
        location = r.substr(a, b - a);
    }
    closesocket(s);
    if (location.empty()) return false;
    std::string path;
    if (!SplitUrl(location, g_host, g_port, path)) return false;
    std::string xml = Http(g_host, g_port, "GET " + path + " HTTP/1.0\r\nHost: " + g_host + "\r\n\r\n", &g_localIp);
    for (const char* type : {"urn:schemas-upnp-org:service:WANIPConnection:", "urn:schemas-upnp-org:service:WANPPPConnection:"}) {
        size_t at = Lower(xml).find(Lower(type));
        if (at == std::string::npos) continue;
        size_t end = xml.find('<', at);
        g_service = xml.substr(at, end - at);
        std::string control = Tag(xml, "controlURL", at);
        if (control.empty()) continue;
        std::string h;
        int p;
        if (SplitUrl(control, h, p, g_control)) {  // an absolute URL
            g_host = h;
            g_port = p;
        } else {
            g_control = control[0] == '/' ? control : "/" + control;
        }
        g_log("upnp: router at %s:%d (%s), this PC is %s", g_host.c_str(), g_port, g_service.c_str(), g_localIp.c_str());
        return true;
    }
    g_log("upnp: %s has no WAN connection service", location.c_str());
    return false;
}

bool Add(const Mapping& m) {
    char args[640];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>%s</NewProtocol>"
             "<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient>"
             "<NewEnabled>1</NewEnabled><NewPortMappingDescription>WarBreeds HD</NewPortMappingDescription>"
             "<NewLeaseDuration>0</NewLeaseDuration>",
             m.port, m.tcp ? "TCP" : "UDP", m.port, g_localIp.c_str());
    return !Soap("AddPortMapping", args).empty();
}

void Worker() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    bool found = Discover();
    if (!found) {
        g_log("upnp: no router answered (UPnP off, or not supported): forward the ports by hand, or use a VPN "
              "(see README.txt)");
    } else {
        std::string ip = Tag(Soap("GetExternalIPAddress", ""), "NewExternalIPAddress");
        if (!ip.empty()) {
            in_addr a;
            a.s_addr = inet_addr(ip.c_str());
            const uint8_t* b = (const uint8_t*)&a;
            bool shared = b[0] == 10 || (b[0] == 100 && (b[1] & 0xc0) == 64) || (b[0] == 172 && (b[1] & 0xf0) == 16) ||
                          (b[0] == 192 && b[1] == 168);
            g_log("upnp: the router's internet address is %s%s", ip.c_str(),
                  shared ? ": a private address, so the internet provider has its own NAT in front (CGNAT); other "
                           "players can't reach this PC (join others' games, or use a VPN)"
                         : "");
        }
    }
    std::unique_lock<std::mutex> lock(g_mu);
    g_failed = !found;
    for (;;) {
        g_cv.wait(lock, [] { return !g_queue.empty(); });
        std::vector<Mapping> todo;
        todo.swap(g_queue);
        lock.unlock();
        for (const Mapping& m : todo) {
            if (!found) continue;
            bool ok = Add(m);
            g_log("upnp: forward %s %d -> %s %s", m.tcp ? "TCP" : "UDP", m.port, g_localIp.c_str(), ok ? "done" : "FAILED");
            if (ok) {
                std::lock_guard<std::mutex> l(g_mu);
                g_done.push_back(m);
            }
        }
        lock.lock();
    }
}

}  // namespace

void Init(void (*log)(const char*, ...)) { g_log = log; }

void Map(int port, bool tcp) {
    std::lock_guard<std::mutex> lock(g_mu);
    for (const Mapping& m : g_done)
        if (m.port == port && m.tcp == tcp) return;
    for (const Mapping& m : g_queue)
        if (m.port == port && m.tcp == tcp) return;
    g_queue.push_back({port, tcp});
    if (!g_started) {
        g_started = true;
        std::thread(Worker).detach();
    }
    g_cv.notify_one();
}

void UnmapAll() {
    std::vector<Mapping> done;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        done.swap(g_done);
    }
    for (const Mapping& m : done) {
        char args[256];
        snprintf(args, sizeof args,
                 "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>%s</NewProtocol>",
                 m.port, m.tcp ? "TCP" : "UDP");
        bool ok = !Soap("DeletePortMapping", args).empty();
        g_log("upnp: removed %s %d %s", m.tcp ? "TCP" : "UDP", m.port, ok ? "" : "(FAILED)");
    }
}

}  // namespace upnp
