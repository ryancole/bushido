#include "netplay/transport.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Winsock wants a process-wide startup/shutdown pair. One static owner is
// enough: the game opens at most one socket, and the teardown rides process
// exit either way.
struct WinsockScope {
    bool ok = false;
    WinsockScope() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!ok) {
            std::fprintf(stderr, "net: WSAStartup failed\n");
        }
    }
    ~WinsockScope() {
        if (ok) {
            WSACleanup();
        }
    }
};

bool winsockReady() {
    static WinsockScope scope;
    return scope.ok;
}

} // namespace

struct UdpTransport::Impl {
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in peer{};
    bool havePeer = false;

    ~Impl() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
    }

    bool open(std::uint16_t bindPort) {
        if (!winsockReady()) {
            return false;
        }
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            std::fprintf(stderr, "net: could not create a socket\n");
            return false;
        }
        u_long nonBlocking = 1;
        if (ioctlsocket(sock, FIONBIO, &nonBlocking) != 0) {
            std::fprintf(stderr, "net: could not set the socket non-blocking\n");
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(bindPort);
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            std::fprintf(stderr, "net: could not bind port %u (in use?)\n", bindPort);
            return false;
        }
        return true;
    }
};

UdpTransport::UdpTransport() : m_impl(std::make_unique<Impl>()) {}
UdpTransport::~UdpTransport() = default;

bool UdpTransport::host(std::uint16_t port) {
    if (!m_impl->open(port)) {
        return false;
    }
    // No peer address yet — the client's first packet supplies it, which is
    // also what makes this work without either side knowing the other's port.
    std::fprintf(stderr, "net: hosting on port %u, waiting for an opponent\n", port);
    return true;
}

bool UdpTransport::join(const char* address, std::uint16_t port) {
    if (!m_impl->open(0)) { // ephemeral: the host learns our port from our packets
        return false;
    }
    // Numeric addresses and hostnames both, so "localhost" works for a
    // two-instances-on-one-box test.
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char portText[16];
    std::snprintf(portText, sizeof portText, "%u", port);
    addrinfo* found = nullptr;
    if (getaddrinfo(address, portText, &hints, &found) != 0 || !found) {
        std::fprintf(stderr, "net: could not resolve '%s'\n", address);
        return false;
    }
    std::memcpy(&m_impl->peer, found->ai_addr, sizeof(sockaddr_in));
    freeaddrinfo(found);
    m_impl->havePeer = true;
    std::fprintf(stderr, "net: joining %s:%u\n", address, port);
    return true;
}

bool UdpTransport::send(const void* data, int bytes) {
    Impl& im = *m_impl;
    if (im.sock == INVALID_SOCKET || !im.havePeer) {
        return false;
    }
    int sent = sendto(im.sock, static_cast<const char*>(data), bytes, 0,
                      reinterpret_cast<const sockaddr*>(&im.peer), sizeof im.peer);
    return sent == bytes;
}

int UdpTransport::receive(void* data, int capacity) {
    Impl& im = *m_impl;
    if (im.sock == INVALID_SOCKET) {
        return 0;
    }
    sockaddr_in from{};
    int fromLen = sizeof from;
    int got = recvfrom(im.sock, static_cast<char*>(data), capacity, 0,
                       reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (got <= 0) {
        return 0; // WSAEWOULDBLOCK on an empty queue, which is the common case
    }
    if (!im.havePeer) {
        // A host adopts the first sender as the opponent. Fine for a direct
        // connection between two people who arranged it; a public listener
        // would want the handshake to authenticate before trusting this.
        im.peer = from;
        im.havePeer = true;
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, text, sizeof text);
        std::fprintf(stderr, "net: opponent at %s:%u\n", text, ntohs(from.sin_port));
    } else if (from.sin_addr.s_addr != im.peer.sin_addr.s_addr ||
               from.sin_port != im.peer.sin_port) {
        return 0; // not our peer; drop it rather than let it drive the sim
    }
    return got;
}

bool UdpTransport::peerKnown() const { return m_impl->havePeer; }

bool parseEndpoint(const char* text, std::string& host, std::uint16_t& port) {
    std::string all(text);
    std::size_t colon = all.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= all.size()) {
        return false;
    }
    char* end = nullptr;
    long value = std::strtol(all.c_str() + colon + 1, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 65535) {
        return false;
    }
    host = all.substr(0, colon);
    port = static_cast<std::uint16_t>(value);
    return true;
}
