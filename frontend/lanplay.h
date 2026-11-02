#pragma once
// LAN PLAY: the DS's local wireless (trades, battles, the Union Room, the Wireless Club) with other
// programs on the same network: another copy of this one (a PC, the Switch, the PS5, a phone), or
// melonDS itself, whose own LAN play this is (melonDS/src/net/LAN.cpp: ENet on port 7064, its
// sessions announced on port 7063). One program hosts (HOST in the pause menu's LAN PLAY), the
// others join it (JOIN: the hosts found on the network); then the games talk as two DSes would.
//
// Each program needs a DS of its own to the games: its own MAC address (the firmware's), which is
// melonDS's same default everywhere unless changed. Here it is made up once per installation
// (lan_mac in the settings file, a Nintendo prefix and five random bytes' worth) and kept.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <atomic>

#include "net/LAN.h"
#include "net/MPInterface.h"
#ifdef __SWITCH__
#include "switch_platform.h"
#endif

namespace melonDS::Platform
{
extern std::atomic<uint32_t> MPSent, MPReceived; // (frontend/platform.cpp)
}

namespace LanPlay
{
enum State
{
    OFF,
    SEARCHING, // looking for hosts (JOIN)
    HOSTING,
    JOINED
};

struct Host
{
    std::string name;    // the host's player name, as its session's
    std::string address; // its IPv4 address
    int players = 0, max = 0;
    bool playing = false;
};

namespace detail
{
inline State& Current()
{
    static State s = OFF;
    return s;
}
inline melonDS::LAN& Lan() { return static_cast<melonDS::LAN&>(melonDS::MPInterface::Get()); }
// melonDS's LAN in place of the core's do-nothing default, with melonDS's own receive timeout.
inline void Use()
{
    if (melonDS::MPInterface::GetType() == melonDS::MPInterface_LAN) return;
#ifdef __SWITCH__
    SwitchPlatform::StartNetwork(); // (libnx's sockets, before ENet's first)
#endif
    melonDS::MPInterface::Set(melonDS::MPInterface_LAN);
    melonDS::MPInterface::Get().SetRecvTimeout(25);
}
} // namespace detail

inline State Now() { return detail::Current(); }
inline std::string LocalAddress();

// HOST: a session the others join, for up to `players` DSes (this one among them).
inline bool StartHost(const std::string& player, int players)
{
    if (Now() == HOSTING || Now() == JOINED) return true;
    detail::Use();
    if (Now() == SEARCHING) detail::Lan().EndDiscovery();
    const bool ok = detail::Lan().StartHost(player.c_str(), players);
    fprintf(stderr, "[lan] hosting as %s for %d: %s\n", player.c_str(), players, ok ? "started" : "failed");
    detail::Current() = ok ? HOSTING : OFF;
    if (ok) fprintf(stderr, "[lan] this machine's address: %s\n", LocalAddress().c_str());
    return ok;
}

// JOIN: the hosts on the network are looked for (Hosts) until one is joined or LEAVE.
inline void Search()
{
    if (Now() != OFF) return;
    detail::Use();
    if (detail::Lan().StartDiscovery())
    {
        detail::Current() = SEARCHING;
        fprintf(stderr, "[lan] looking for hosts\n");
    }
    else fprintf(stderr, "[lan] could not look for hosts\n");
}

inline std::vector<Host> Hosts()
{
    std::vector<Host> out;
    if (Now() != SEARCHING) return out;
    for (const auto& [addr, d] : detail::Lan().GetDiscoveryList())
    {
        Host h;
        char name[sizeof d.SessionName + 1] = {};
        memcpy(name, d.SessionName, sizeof d.SessionName);
        h.name = name;
        char a[16];
        snprintf(a, sizeof a, "%u.%u.%u.%u", addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);
        h.address = a;
        h.players = d.NumPlayers;
        h.max = d.MaxPlayers;
        h.playing = d.Status == 1;
        out.push_back(h);
    }
    return out;
}

// Joins the host at that address (one of Hosts, or typed); false if it could not be reached.
inline bool Join(const std::string& player, const std::string& address)
{
    detail::Use();
    if (Now() == SEARCHING) detail::Lan().EndDiscovery();
    const bool ok = detail::Lan().StartClient(player.c_str(), address.c_str());
    fprintf(stderr, "[lan] joining %s as %s: %s\n", address.c_str(), player.c_str(), ok ? "joined" : "failed");
    detail::Current() = ok ? JOINED : OFF;
    return ok;
}

// LEAVE: the session (or the search) ended, and the core back to no wireless.
inline void Leave()
{
    if (Now() == OFF) return;
    if (Now() == SEARCHING) detail::Lan().EndDiscovery();
    else detail::Lan().EndSession();
    melonDS::MPInterface::Set(melonDS::MPInterface_Dummy);
    detail::Current() = OFF;
    fprintf(stderr, "[lan] left\n");
}

// This machine's address on the network (for melonDS's DIRECT CONNECT to a host here), or "":
// the address a datagram socket gets when pointed at the internet (nothing is sent).
inline std::string LocalAddress()
{
    if (Now() == OFF) return ""; // (ENet is started with melonDS's LAN)
    const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) return "";
    ENetAddress to;
    enet_address_set_host_ip(&to, "8.8.8.8");
    to.port = 53;
    std::string out;
    ENetAddress me;
    char ip[64];
    if (enet_socket_connect(s, &to) == 0 && enet_socket_get_address(s, &me) == 0 &&
        enet_address_get_host_ip(&me, ip, sizeof ip) == 0 && strcmp(ip, "0.0.0.0") != 0)
        out = ip;
    enet_socket_destroy(s);
    return out;
}

// The players in the session (hosting or joined), by name.
inline std::vector<std::string> Players()
{
    std::vector<std::string> out;
    if (Now() != HOSTING && Now() != JOINED) return out;
    for (const melonDS::LAN::Player& p : detail::Lan().GetPlayerList())
    {
        char name[sizeof p.Name + 1] = {};
        memcpy(name, p.Name, sizeof p.Name);
        out.push_back(std::string(name) + (p.IsLocalPlayer ? " (YOU)" : ""));
    }
    return out;
}

// Once a video frame: the network's packets and the hosts' announcements; every 600 frames (ten
// seconds) the DS packets the game sent and got in that time, in game.log (frontend/platform.cpp
// counts them): none while it is not using its wireless.
inline void Process()
{
    if (Now() == OFF) return;
    melonDS::MPInterface::Get().Process();
    static int frames = 0;
    if (++frames < 600) return;
    frames = 0;
    if (Now() != HOSTING && Now() != JOINED) return;
    const uint32_t sent = melonDS::Platform::MPSent.exchange(0), got = melonDS::Platform::MPReceived.exchange(0);
    fprintf(stderr, "[lan] %zu players; in 10 s the game sent %u packets and got %u\n", Players().size(), sent, got);
}

// This installation's MAC address for the firmware: lan_mac in the settings file at cfgPath,
// made up (and added to the file) the first time. Nintendo's prefix 00:09:BF, as a DS's.
inline std::array<uint8_t, 6> MacFor(const std::string& cfgPath)
{
    std::array<uint8_t, 6> mac = { 0x00, 0x09, 0xBF, 0, 0, 0 };
    if (FILE* f = fopen(cfgPath.c_str(), "r"))
    {
        char line[256];
        unsigned v[6];
        bool found = false;
        while (!found && fgets(line, sizeof line, f))
            if (sscanf(line, "lan_mac=%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
            {
                for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
                found = true;
            }
        fclose(f);
        if (found) return mac;
    }
    std::random_device rd;
    for (int i = 3; i < 6; i++) mac[i] = (uint8_t)(rd() & 0xFF);
    if (FILE* f = fopen(cfgPath.c_str(), "a"))
    {
        fprintf(f, "lan_mac=%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        fclose(f);
    }
    return mac;
}

// The name the others see: the machine's kind and the end of this one's MAC address.
inline std::string PlayerName(const std::array<uint8_t, 6>& mac)
{
#if defined(SSR_PS5)
    const char* kind = "PS5";
#elif defined(__SWITCH__)
    const char* kind = "SWITCH";
#elif defined(__ANDROID__)
    const char* kind = "ANDROID";
#elif defined(SSR_IOS)
    const char* kind = "IPHONE";
#else
    const char* kind = "PC";
#endif
    char name[32];
    snprintf(name, sizeof name, "%s %02X%02X", kind, mac[4], mac[5]);
    return name;
}
} // namespace LanPlay
