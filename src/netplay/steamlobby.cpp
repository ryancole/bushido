#include "netplay/steamlobby.hpp"

#include <cstdio>

#ifdef BUSHIDO_STEAM

#include <steam/steam_api.h>

struct SteamLobby::Impl {
    State state = State::Idle;
    CSteamID lobby;
    CSteamID peer;
    bool joinRequested = false;
    std::uint64_t requestedLobby = 0;
    char status[96] = "";

    void say(State s, const char* text) {
        state = s;
        std::snprintf(status, sizeof status, "%s", text);
    }

    // CreateLobby and JoinLobby answer asynchronously, so both need a
    // CCallResult rather than a plain callback.
    CCallResult<Impl, LobbyCreated_t> createResult;
    CCallResult<Impl, LobbyEnter_t> enterResult;

    void onCreated(LobbyCreated_t* created, bool ioFailure) {
        if (ioFailure || created->m_eResult != k_EResultOK) {
            say(State::Failed, "Steam could not open a lobby");
            return;
        }
        lobby = CSteamID(created->m_ulSteamIDLobby);
        // Version stamp: a friend on a different build should find that out
        // here rather than at a protocol mismatch two screens later.
        SteamMatchmaking()->SetLobbyData(lobby, "game", "bushido");
        say(State::Hosting, "lobby open - invite a friend");
        std::fprintf(stderr, "steam: lobby %llu created\n",
                     static_cast<unsigned long long>(lobby.ConvertToUint64()));
    }

    void onEntered(LobbyEnter_t* entered, bool ioFailure) {
        if (ioFailure ||
            entered->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            say(State::Failed, "could not join that lobby");
            return;
        }
        lobby = CSteamID(entered->m_ulSteamIDLobby);
        peer = SteamMatchmaking()->GetLobbyOwner(lobby);
        if (peer == SteamUser()->GetSteamID()) {
            // Our own lobby: hosting, not joining. Nothing to connect to.
            say(State::Hosting, "lobby open - invite a friend");
            return;
        }
        say(State::Joined, "joined - connecting");
        std::fprintf(stderr, "steam: joined lobby %llu, host is %llu\n",
                     static_cast<unsigned long long>(lobby.ConvertToUint64()),
                     static_cast<unsigned long long>(peer.ConvertToUint64()));
    }

    // Fired when a friend accepts an invite while the game is already up. If
    // it is not running, Steam launches it with +connect_lobby instead, which
    // main parses off the command line.
    STEAM_CALLBACK(Impl, onJoinRequested, GameLobbyJoinRequested_t);
};

void SteamLobby::Impl::onJoinRequested(GameLobbyJoinRequested_t* request) {
    requestedLobby = request->m_steamIDLobby.ConvertToUint64();
    joinRequested = true;
    std::fprintf(stderr, "steam: invited to lobby %llu\n",
                 static_cast<unsigned long long>(requestedLobby));
}

SteamLobby::SteamLobby() : m_impl(std::make_unique<Impl>()) {}

SteamLobby::~SteamLobby() { leave(); }

bool SteamLobby::host() {
    if (!SteamMatchmaking()) {
        return false;
    }
    // Friends-only and two seats: this is a duel, and an open lobby would let
    // a stranger take the other one.
    SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, 2);
    m_impl->createResult.Set(call, m_impl.get(), &Impl::onCreated);
    m_impl->say(State::Creating, "opening a lobby...");
    return true;
}

bool SteamLobby::join(std::uint64_t lobbyId) {
    if (!SteamMatchmaking() || lobbyId == 0ull) {
        return false;
    }
    SteamAPICall_t call = SteamMatchmaking()->JoinLobby(CSteamID(lobbyId));
    m_impl->enterResult.Set(call, m_impl.get(), &Impl::onEntered);
    m_impl->say(State::Joining, "joining...");
    return true;
}

void SteamLobby::leave() {
    if (m_impl->lobby.IsValid() && SteamMatchmaking()) {
        SteamMatchmaking()->LeaveLobby(m_impl->lobby);
    }
    m_impl->lobby = CSteamID();
    m_impl->peer = CSteamID();
    m_impl->say(State::Idle, "");
}

SteamLobby::State SteamLobby::state() const { return m_impl->state; }

std::uint64_t SteamLobby::peerId() const {
    return m_impl->peer.ConvertToUint64();
}

std::uint64_t SteamLobby::lobbyId() const {
    return m_impl->lobby.ConvertToUint64();
}

bool SteamLobby::openInviteOverlay() const {
    if (!SteamFriends() || !m_impl->lobby.IsValid()) {
        return false;
    }
    if (!SteamUtils()->IsOverlayEnabled()) {
        return false; // the caller has to say so; nothing visible would happen
    }
    SteamFriends()->ActivateGameOverlayInviteDialog(m_impl->lobby);
    return true;
}

bool SteamLobby::takeJoinRequest(std::uint64_t& lobbyId) {
    if (!m_impl->joinRequested) {
        return false;
    }
    m_impl->joinRequested = false;
    lobbyId = m_impl->requestedLobby;
    return true;
}

const char* SteamLobby::status() const { return m_impl->status; }

#else // no Steamworks SDK in this build

struct SteamLobby::Impl {};

SteamLobby::SteamLobby() = default;
SteamLobby::~SteamLobby() = default;
bool SteamLobby::host() { return false; }
bool SteamLobby::join(std::uint64_t) { return false; }
void SteamLobby::leave() {}
SteamLobby::State SteamLobby::state() const { return State::Idle; }
std::uint64_t SteamLobby::peerId() const { return 0ull; }
std::uint64_t SteamLobby::lobbyId() const { return 0ull; }
bool SteamLobby::openInviteOverlay() const { return false; }
bool SteamLobby::takeJoinRequest(std::uint64_t&) { return false; }
const char* SteamLobby::status() const { return ""; }

#endif
