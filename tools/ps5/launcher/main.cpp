// Pokemon Fulldex's app on the PS5's Home screen: a native application (built with
// ps5-native-app-boilerplate, registered by ShadowMountPlus from /data/homebrew/PKFD00001)
// whose only work is to start the program. It sends starter.elf, beside it in its folder, to the
// console's ELF loader (elfldr, loopback port 9021); the starter closes this app and starts
// eboot.elf, the program, as websrv's homebrew list does (tools/ps5/starter/main.c). The app
// itself runs in the system's sandbox, which the program could not: it needs the rights a
// payload has (its JIT, /data, the graphics chip).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "demo_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

extern "C"
{
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetConnect(int socket, const void *address, std::uint32_t address_length);
    int sceNetSend(int socket, const void *data, std::size_t length, int flags);
    int sceNetSetsockopt(int socket, int level, int option, const void *value, std::uint32_t size);
    int sceNetSocketClose(int socket);
}

namespace
{
struct NetSockaddrIn
{
    std::uint8_t length;
    std::uint8_t family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint16_t vport;
    std::uint8_t zero[6];
};

const char *problem = nullptr;

// starter.elf to the loader, whole; false if there is no loader to take it.
bool send_starter()
{
    const int file = open("/app0/starter.elf", O_RDONLY);
    if (file < 0)
    {
        problem = "STARTER.ELF IS MISSING";
        return false;
    }
    const int socket = sceNetSocket("fulldex_starter", 2, 1, 6);
    bool sent = false;
    if (socket >= 0)
    {
        const int timeout_us = 5000000;
        for (const int option : {0x1105, 0x1106, 0x1109}) // (send, receive and connect timeouts)
            (void)sceNetSetsockopt(socket, 0xffff, option, &timeout_us, sizeof(timeout_us));
        constexpr std::uint16_t port = 9021;
        const NetSockaddrIn loader{sizeof(NetSockaddrIn), 2, static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                                   0x0100007f, 0, {0}};
        if (sceNetConnect(socket, &loader, sizeof(loader)) >= 0)
        {
            sent = true;
            std::array<std::uint8_t, 16384> buffer{};
            for (;;)
            {
                const ssize_t count = read(file, buffer.data(), buffer.size());
                if (count <= 0)
                {
                    sent = count == 0;
                    break;
                }
                for (ssize_t done = 0; done < count;)
                {
                    const int moved = sceNetSend(socket, buffer.data() + done, static_cast<std::size_t>(count - done), 0);
                    if (moved <= 0)
                    {
                        sent = false;
                        break;
                    }
                    done += moved;
                }
                if (!sent)
                    break;
            }
        }
        (void)sceNetSocketClose(socket);
    }
    close(file);
    if (!sent)
        problem = "NO ELF LOADER ON PORT 9021";
    return sent;
}

void draw(ps5::demo::Canvas &canvas) noexcept
{
    using ps5::demo::Color;
    canvas.clear(Color::background);
    canvas.text(120, 120, "POKEMON FULLDEX", 10, Color::white);
    if (problem == nullptr)
    {
        canvas.text(120, 320, "STARTING...", 6, Color::cyan);
        return;
    }
    canvas.text(120, 320, "COULD NOT START:", 6, Color::yellow);
    canvas.text(120, 420, problem, 6, Color::yellow);
    canvas.text(120, 600, "LOAD ETAHEN OR ELFLDR, THEN START IT AGAIN.", 4, Color::white);
}
} // namespace

int main()
{
    // The starter closes this app once it has the program on its way; until then (and when it
    // could not be sent) the screen says what is happening.
    send_starter();
    ps5::demo::run(draw, problem == nullptr ? "Pokemon Fulldex: starting" : "Pokemon Fulldex: could not start");
}
