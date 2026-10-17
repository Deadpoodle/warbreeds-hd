#pragma once
// Multiplayer networking fixes: one DirectPlay (Windows', never GOG's stub), reply
// addresses corrected for players behind a router (NAT), router ports opened by
// UPnP. See net.cpp.

namespace net {

void Install(void (*log)(const char*, ...), bool netLog, bool natFix, bool upnp);
// From the game's DirectPlay CoCreateInstance (main.cpp): before and after it.
void BeforeDirectPlay();
void AfterDirectPlay();
// At exit: removes the router port mappings made.
void Shutdown();

}  // namespace net
