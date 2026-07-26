#pragma once

#include <cstdint>
#include <memory>

// Steam matchmaking: how two people *find* each other, as opposed to how bytes
// get between them once they have. Deliberately not part of SteamTransport —
// that moves datagrams to a peer it has been told about, and would learn
// nothing from also knowing what a lobby is.
//
// The point of it is that nobody types a seventeen-digit number. A host makes
// a friends-only lobby for two and opens Steam's own invite dialog; the friend
// clicks through in the overlay, or from their friends list, and arrives with
// the lobby id. Whoever joins reads the lobby owner and connects to them, so
// the host still only has to listen.
//
// Assumes SteamAPI_Init has already run (SteamTransport::start does it) and
// that something calls SteamAPI_RunCallbacks each frame — SteamTransport::pump
// does, and it drives these callbacks too. Compiles to a stub without the SDK.
class SteamLobby {
public:
    enum class State {
        Idle,
        Creating, // asked Steam for a lobby, waiting on the answer
        Hosting,  // lobby is up and invitable
        Joining,  // entering someone else's
        Joined,   // in, and peerId() is the host
        Failed,
    };

    SteamLobby();
    ~SteamLobby();
    SteamLobby(const SteamLobby&) = delete;
    SteamLobby& operator=(const SteamLobby&) = delete;

    // Friends-only, room for two. Asynchronous: watch state().
    bool host();
    // Enter a lobby someone invited us to.
    bool join(std::uint64_t lobbyId);
    void leave();

    State state() const;
    // Valid once Joined: the lobby's owner, which is who to connect to.
    std::uint64_t peerId() const;
    std::uint64_t lobbyId() const;

    // Steam's own friend picker, over the game. False if the overlay is
    // unavailable — which it is often enough (disabled in settings, or the
    // game was not launched through Steam) that it needs saying out loud
    // rather than looking like nothing happened.
    bool openInviteOverlay() const;

    // A friend clicked Join while we were already running. Returns the lobby
    // to enter, once, so the caller can act on it and forget it.
    bool takeJoinRequest(std::uint64_t& lobbyId);

    const char* status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
