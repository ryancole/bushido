#pragma once

#include <cstdint>
#include <memory>
#include <string>

// A datagram pipe to one peer: unreliable, unordered, no delivery guarantee.
//
// That is all lockstep needs. Every packet carries the last several frames of
// input rather than just the newest, so a dropped one is covered by the next
// to arrive and there is nothing to retransmit — a reliability layer would add
// head-of-line blocking to a protocol whose whole job is to not block.
//
// The interface exists so the transport underneath can change without the
// session noticing. Today it is direct-IP UDP, which is LAN or port-forward
// only; Steam's ISteamNetworkingSockets (or GameNetworkingSockets standalone,
// whose API is identical) is the shape that replaces it, and brings the NAT
// punch-through and relay this one has no answer for.
class Transport {
public:
    virtual ~Transport() = default;

    // Non-blocking. False if the datagram could not be handed to the OS —
    // which lockstep treats as a drop, because that is what it is.
    virtual bool send(const void* data, int bytes) = 0;

    // Non-blocking. Bytes of the datagram read, or 0 when nothing is waiting.
    virtual int receive(void* data, int capacity) = 0;

    // Whether there is an address to send to yet. A host does not have one
    // until the client's first packet arrives.
    virtual bool peerKnown() const = 0;
};

// Direct-IP UDP over Winsock. Pimpl so <winsock2.h> stays in the .cpp — the
// same reason Physics keeps Jolt out of its header.
class UdpTransport : public Transport {
public:
    UdpTransport();
    ~UdpTransport() override;
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    // Bind `port` and answer whoever speaks first.
    bool host(std::uint16_t port);
    // Bind an ephemeral port and aim at `address`:`port`.
    bool join(const char* address, std::uint16_t port);

    bool send(const void* data, int bytes) override;
    int receive(void* data, int capacity) override;
    bool peerKnown() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Splits "host:port" — the form --join takes. False if there is no port, or
// it is not a number in range. The address half is not resolved here.
bool parseEndpoint(const char* text, std::string& host, std::uint16_t& port);
