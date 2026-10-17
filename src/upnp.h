#pragma once
// UPnP port forwarding on the player's router (upnp.cpp).

namespace upnp {

void Init(void (*log)(const char*, ...));
// Forward this port (TCP or UDP) to this PC; on a background thread, once each.
void Map(int port, bool tcp);
// Remove every mapping made (at exit).
void UnmapAll();

}  // namespace upnp
