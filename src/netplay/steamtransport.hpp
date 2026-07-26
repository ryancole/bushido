#pragma once

#include "netplay/transport.hpp"

#include <cstdint>
#include <memory>

// Transport over Steam Datagram Relay.
//
// This is the one that makes the game playable between two houses. UdpTransport
// needs a forwarded port because nothing in it can cross a home router;
// SteamTransport addresses a *person* rather than an address — a SteamID — and
// Valve's relay carries the packets, punching through or bouncing off their
// network as needed. The session above does not notice the difference: it still
// sends and receives datagrams that may be lost, duplicated or reordered, which
// is all lockstep ever needed.
//
// Built on ISteamNetworkingMessages rather than ISteamNetworkingSockets: the
// former is connectionless send-to-a-peer, which is already the shape of
// Transport, and skips the connection-state machine we would only flatten again.
//
// The Steamworks SDK is licence-gated and cannot be fetched by the build, so
// everything here compiles to a stub unless BUSHIDO_STEAM is defined — see
// STEAM_SDK_DIR in CMakeLists.txt. `available()` is the honest test.
class SteamTransport : public Transport {
public:
    SteamTransport();
    ~SteamTransport() override;
    SteamTransport(const SteamTransport&) = delete;
    SteamTransport& operator=(const SteamTransport&) = delete;

    // Built with the SDK *and* Steam running and willing to talk to us.
    static bool available();

    // Bring up the Steam API and warm the relay. False if Steam is not running.
    // Safe to call again after a failure — nothing is initialised by one — and
    // that is how a player who started the client after the game gets the relay
    // without restarting.
    bool start();
    // Why Steam is, or is not, usable — a sentence fit to put on screen.
    // `start` fills it in either way.
    //
    // This exists because the failure had no reader. A packaged build's stderr
    // goes to a console window nobody looks at, so a friend whose Steam was not
    // running got the direct-IP screen with nothing anywhere saying why, and no
    // way to tell that from a build made without the SDK at all.
    const char* status() const;
    // Accept whoever messages us first — the host side. `start` must have run.
    bool listen();
    // Address a specific person. The host's SteamID is what gets shared.
    bool connectTo(std::uint64_t peerSteamId);
    // This machine's SteamID, which is what a host reads out to a friend.
    //
    // There is no local loopback to be had here, and both ways of trying were
    // measured rather than assumed. A process messaging its *own* SteamID
    // makes Steam assert (csteamnetworkingmessages.cpp:233) and drop the
    // session; an IP identity is refused outright ("Bad FakeIP type", result
    // 5003), because ISteamNetworkingMessages is SteamID-only and IP
    // addressing belongs to ISteamNetworkingSockets. Testing through *that*
    // would exercise an API this file does not use. Two machines and two
    // accounts is the only honest test.
    std::uint64_t selfId() const;

    // Steam's callbacks only fire from here; call once per frame.
    void pump();

    bool send(const void* data, int bytes) override;
    int receive(void* data, int capacity) override;
    bool peerKnown() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
