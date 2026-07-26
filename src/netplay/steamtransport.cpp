#include "netplay/steamtransport.hpp"

#include <cstdio>

#ifdef BUSHIDO_STEAM

#include <steam/steam_api.h>

#include <cstring>

namespace {
// One channel is enough: the session multiplexes everything it needs into the
// packet itself.
constexpr int kChannel = 0;
} // namespace

struct SteamTransport::Impl {
    bool up = false;
    char status[192] = "Steam has not been asked yet.";
    bool havePeer = false;
    SteamNetworkingIdentity peer{};
    bool accepting = false; // host: take the first person who calls

    // The callback macros need the Steam headers, which is why this sits in
    // the .cpp behind the pimpl rather than in the class the rest of the game
    // can see.
    struct Hooks {
        Impl* owner = nullptr;
        explicit Hooks(Impl* o) : owner(o) {}
        STEAM_CALLBACK(Hooks, onSessionRequest, SteamNetworkingMessagesSessionRequest_t);
        STEAM_CALLBACK(Hooks, onSessionFailed, SteamNetworkingMessagesSessionFailed_t);
    };
    std::unique_ptr<Hooks> hooks;
};

void SteamTransport::Impl::Hooks::onSessionRequest(
    SteamNetworkingMessagesSessionRequest_t* request) {
    if (!owner->accepting || owner->havePeer) {
        return; // a match already has its two people in it
    }
    if (!SteamNetworkingMessages()->AcceptSessionWithUser(request->m_identityRemote)) {
        return;
    }
    owner->peer = request->m_identityRemote;
    owner->havePeer = true;
    std::fprintf(stderr, "steam: opponent %llu connected\n",
                 static_cast<unsigned long long>(
                     request->m_identityRemote.GetSteamID64()));
}

void SteamTransport::Impl::Hooks::onSessionFailed(
    SteamNetworkingMessagesSessionFailed_t* failed) {
    // Losing the session is not fatal here: the lockstep layer already treats
    // silence as a stall and then a timeout, and says so in words.
    std::fprintf(stderr, "steam: session failed (%d)\n",
                 failed->m_info.m_eEndReason);
}

SteamTransport::SteamTransport() : m_impl(std::make_unique<Impl>()) {}

SteamTransport::~SteamTransport() {
    if (m_impl->up) {
        m_impl->hooks.reset();
        SteamAPI_Shutdown();
    }
}

bool SteamTransport::available() { return true; }

bool SteamTransport::start() {
    if (m_impl->up) {
        return true;
    }
    // InitEx rather than Init: the plain one collapses every failure into
    // `false`, and "no Steam options" is exactly the complaint that needs the
    // difference between a client that is not running, one that is too old, and
    // an app id the working directory does not have. Silent otherwise — Steam
    // is tried on every launch now, so "not running" is an ordinary answer
    // rather than an error, and the caller decides whether to say so.
    SteamErrMsg err = {};
    const ESteamAPIInitResult result = SteamAPI_InitEx(&err);
    if (result != k_ESteamAPIInitResult_OK) {
        // Steam's own message is worth carrying only where it says something
        // the result code did not; the two named cases are already sentences.
        switch (result) {
        case k_ESteamAPIInitResult_NoSteamClient:
            std::snprintf(m_impl->status, sizeof m_impl->status,
                          "Steam is not running.");
            break;
        case k_ESteamAPIInitResult_VersionMismatch:
            std::snprintf(m_impl->status, sizeof m_impl->status,
                          "The Steam client is out of date.");
            break;
        default:
            std::snprintf(m_impl->status, sizeof m_impl->status,
                          "Steam refused to start: %s", err);
            break;
        }
        return false;
    }
    m_impl->hooks = std::make_unique<Impl::Hooks>(m_impl.get());
    // Relay tickets take a moment to fetch; asking now means the first match
    // does not pay for it.
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    m_impl->up = true;
    std::snprintf(m_impl->status, sizeof m_impl->status, "Steam is ready (%llu).",
                  static_cast<unsigned long long>(selfId()));
    std::fprintf(stderr, "steam: ready, this machine is %llu\n",
                 static_cast<unsigned long long>(selfId()));
    return true;
}

const char* SteamTransport::status() const { return m_impl->status; }

bool SteamTransport::listen() {
    if (!m_impl->up) {
        return false;
    }
    m_impl->accepting = true;
    std::fprintf(stderr, "steam: waiting for an opponent - give them %llu\n",
                 static_cast<unsigned long long>(selfId()));
    return true;
}

bool SteamTransport::connectTo(std::uint64_t peerSteamId) {
    if (!m_impl->up) {
        return false;
    }
    m_impl->peer.Clear();
    m_impl->peer.SetSteamID64(peerSteamId);
    m_impl->havePeer = true;
    m_impl->accepting = false;
    // No connect step: the first message opens the session, and the peer's
    // callback accepts it.
    std::fprintf(stderr, "steam: joining %llu\n",
                 static_cast<unsigned long long>(peerSteamId));
    return true;
}

std::uint64_t SteamTransport::selfId() const {
    return m_impl->up ? SteamUser()->GetSteamID().ConvertToUint64() : 0ull;
}


void SteamTransport::pump() {
    if (m_impl->up) {
        SteamAPI_RunCallbacks();
    }
}

bool SteamTransport::send(const void* data, int bytes) {
    if (!m_impl->up || !m_impl->havePeer) {
        return false;
    }
    // Unreliable and unordered on purpose: every packet the session sends is
    // self-contained, so a retransmit would only ever arrive too late to use.
    EResult sent = SteamNetworkingMessages()->SendMessageToUser(
        m_impl->peer, data, static_cast<uint32>(bytes),
        k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle, kChannel);
    return sent == k_EResultOK;
}

int SteamTransport::receive(void* data, int capacity) {
    if (!m_impl->up) {
        return 0;
    }
    SteamNetworkingMessage_t* msg = nullptr;
    if (SteamNetworkingMessages()->ReceiveMessagesOnChannel(kChannel, &msg, 1) != 1) {
        return 0;
    }
    int size = static_cast<int>(msg->m_cbSize);
    if (size > capacity) {
        size = capacity;
    }
    std::memcpy(data, msg->m_pData, static_cast<std::size_t>(size));
    if (!m_impl->havePeer) {
        m_impl->peer = msg->m_identityPeer;
        m_impl->havePeer = true;
    }
    msg->Release();
    return size;
}

bool SteamTransport::peerKnown() const { return m_impl->havePeer; }

#else // no Steamworks SDK in this build

struct SteamTransport::Impl {};

SteamTransport::SteamTransport() = default;
SteamTransport::~SteamTransport() = default;

bool SteamTransport::available() { return false; }

bool SteamTransport::start() { return false; }

const char* SteamTransport::status() const {
    return "This build was made without the Steamworks SDK.";
}

bool SteamTransport::listen() { return false; }
bool SteamTransport::connectTo(std::uint64_t) { return false; }
std::uint64_t SteamTransport::selfId() const { return 0ull; }
void SteamTransport::pump() {}
bool SteamTransport::send(const void*, int) { return false; }
int SteamTransport::receive(void*, int) { return 0; }
bool SteamTransport::peerKnown() const { return false; }

#endif
