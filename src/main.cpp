#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>

#include "audio.hpp"
#include "bot.hpp"
#include "camera.hpp"
#include "characters/character.hpp"
#include "config.hpp"
#include "game.hpp"
#include "input.hpp"
#include "levels/level.hpp"
#include "netplay/replay.hpp"
#include "netplay/session.hpp"
#include "netplay/simlink.hpp"
#include "netplay/steamtransport.hpp"
#include "netplay/transport.hpp"
#include "renderer.hpp"
#include "samurai.hpp"
#include "weapons/weapon.hpp"

namespace {

enum class AppState {
    Loading,
    Menu,
    Options,
    NetSetup, // host-or-join, before anyone picks a fighter
    CharacterSelect,
    NetWait, // picked, waiting on the handshake to agree the match
    Playing
};
enum class MenuAction { None, Play, Versus, Online, Options, Quit };

// How the match being set up will be driven. Chosen when the flow starts — a
// menu button or a command-line flag — and read once the select screen
// finishes, which is the only place the four modes differ.
enum class SetupMode { Solo, LocalVersus, NetHost, NetClient };

// One piece of startup work. The app opens straight into `Loading` and runs
// exactly one step per frame, so the window stays responsive and the bar
// advances on real completions rather than on a timer. `weight` is the step's
// measured cost in milliseconds (debug build, one machine) — the steps are
// wildly uneven, opening the audio device costing several times what every
// other step costs put together while a level prewarm rounds to nothing, so a
// bar driven by step *count* would sit still and then leap. The numbers only
// have to hold their ratios:
// they're a shape for the bar, not a promise about the clock.
struct LoadStep {
    const char* label;
    float weight;
    std::function<void()> run;
};

// Select-screen state. Three stages on one screen: pick the fighter, then the
// blade, then the battleground. `shown*` are the indices being previewed (the
// last tile the mouse hovered); each picked* latches once its tile is clicked
// and flips the screen to the next row.
struct SelectScreen {
    int shown = 0;
    int shownWeapon = 0;
    int shownLevel = 0;
    int pickedCharacter = -1;
    int pickedWeapon = -1;
    // Who is picking and what for. `player` is 0-based and only reaches the
    // titles (local versus runs this screen once per human); `mode` decides
    // what the last click leads to, which is the one thing the hint line has
    // to be honest about; `pickLevel` is false for whoever picks into a ground
    // someone else already settled, making their blade the final click.
    int player = 0;
    SetupMode mode = SetupMode::Solo;
    bool pickLevel = true;
};

// A completed pick, returned by drawCharacterSelect once the level (the last
// stage) is clicked; character stays -1 until then.
struct SelectResult {
    int character = -1;
    int weapon = -1;
    int level = -1;
};

// The default ImGui look is a grey debug tool; restyle it into a sparse
// menu that sits over the arena scene. Runs once, after the ImGui context
// exists (renderer.init) and before the first frame.
void setupMenuStyle() {
    // A serif system font suits the theme better than ProggyClean; fall back
    // silently if it's missing (font size is overridden per-widget anyway).
    const char* fontPath = "C:/Windows/Fonts/georgia.ttf";
    FILE* probe = nullptr;
    if (fopen_s(&probe, fontPath, "rb") == 0 && probe) {
        std::fclose(probe);
        ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath, 20.0f);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = {48.0f, 40.0f};
    style.FramePadding = {0.0f, 14.0f};
    style.ItemSpacing = {0.0f, 16.0f};
    style.FrameRounding = 2.0f;
    style.FrameBorderSize = 1.0f;
    style.Colors[ImGuiCol_Text] = {0.91f, 0.87f, 0.80f, 1.0f};
    style.Colors[ImGuiCol_Border] = {0.91f, 0.87f, 0.80f, 0.25f};
    style.Colors[ImGuiCol_Button] = {0.0f, 0.0f, 0.0f, 0.35f};
    style.Colors[ImGuiCol_ButtonHovered] = {0.55f, 0.09f, 0.09f, 0.85f};
    style.Colors[ImGuiCol_ButtonActive] = {0.72f, 0.13f, 0.13f, 1.0f};
    style.Colors[ImGuiCol_NavCursor] = {0.85f, 0.70f, 0.25f, 0.80f};
    // Slider troughs match the button faces, and the grab picks up the riposte
    // gold while it's being dragged (the options screen's volume rows).
    style.Colors[ImGuiCol_FrameBg] = {0.0f, 0.0f, 0.0f, 0.35f};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.55f, 0.09f, 0.09f, 0.50f};
    style.Colors[ImGuiCol_FrameBgActive] = {0.55f, 0.09f, 0.09f, 0.70f};
    style.Colors[ImGuiCol_SliderGrab] = {0.72f, 0.13f, 0.13f, 1.0f};
    style.Colors[ImGuiCol_SliderGrabActive] = {0.85f, 0.70f, 0.25f, 1.0f};
}

// Loading screen: the title over a progress bar and the name of the work in
// hand. `fill` is the eased bar position (0..1) and `label` the step about to
// run. Drawn over the bare clear color — at this point the arena the menus sit
// in front of hasn't been built yet.
void drawLoadingScreen(float fill, const char* label) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("loading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    // Same title as the main menu, so the screen it hands off to looks like the
    // same screen with the bar swapped for the buttons.
    ImGui::PushFont(nullptr, 88.0f);
    const float titleWidth = ImGui::CalcTextSize("BUSHIDO").x;
    ImGui::TextUnformatted("BUSHIDO");
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 10.0f});

    // The bar borrows the blood bars' palette — black trough, crimson fill,
    // cream hairline — so the HUD and the front end speak the same language.
    // Drawn straight to the draw list; ImGui only reserves the space.
    const float barW = titleWidth, barH = 12.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx{mn.x + barW, mn.y + barH};
    dl->AddRectFilled(mn, mx, IM_COL32(0, 0, 0, 130), 3.0f);
    if (fill > 0.0f) {
        dl->AddRectFilled(mn, {mn.x + barW * fill, mx.y}, IM_COL32(214, 44, 44, 220),
                          3.0f);
    }
    dl->AddRect(mn, mx, IM_COL32(232, 222, 204, 70), 3.0f, 0, 1.5f);
    ImGui::Dummy({barW, barH});

    // Step name on the left, percentage right-aligned to the bar's end.
    ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.65f});
    ImGui::PushFont(nullptr, 17.0f);
    ImGui::TextUnformatted(label);
    char percent[8];
    // Rounded, not truncated: the bar hands off a hair under full, and a
    // loading screen whose last word is "99%" reads as a stall.
    std::snprintf(percent, sizeof(percent), "%d%%",
                  static_cast<int>(fill * 100.0f + 0.5f));
    ImGui::SameLine(ImGui::GetStyle().WindowPadding.x + barW -
                    ImGui::CalcTextSize(percent).x);
    ImGui::TextUnformatted(percent);
    ImGui::PopFont();
    ImGui::PopStyleColor();

    ImGui::End();
}

// Roster indices that arrived from outside this process — a replay file, a
// netplay peer — get checked before they reach a table lookup.
bool setupIsSane(const MatchSetup& s) {
    if (s.level < 0 || s.level >= kLevelCount) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (s.chars[i] < 0 || s.chars[i] >= kCharacterCount) {
            return false;
        }
        if (s.weapons[i] < 0 || s.weapons[i] >= kWeaponCount) {
            return false;
        }
    }
    return true;
}

MenuAction drawMainMenu() {
    MenuAction action = MenuAction::None;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("main-menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 88.0f);
    const float titleWidth = ImGui::CalcTextSize("BUSHIDO").x;
    ImGui::TextUnformatted("BUSHIDO");
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 12.0f});

    // Center the buttons under the title (which sets the window's width).
    const float buttonWidth = 240.0f;
    const float buttonX =
        ImGui::GetStyle().WindowPadding.x + (titleWidth - buttonWidth) * 0.5f;

    ImGui::PushFont(nullptr, 30.0f);
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Play", {buttonWidth, 0.0f})) {
        action = MenuAction::Play;
    }
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Local Versus", {buttonWidth, 0.0f})) {
        action = MenuAction::Versus;
    }
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Online", {buttonWidth, 0.0f})) {
        action = MenuAction::Online;
    }
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Options", {buttonWidth, 0.0f})) {
        action = MenuAction::Options;
    }
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Quit", {buttonWidth, 0.0f})) {
        action = MenuAction::Quit;
    }
    ImGui::PopFont();

    ImGui::End();
    return action;
}

// Online setup: host on a port, or join an address. Kept deliberately plain —
// this is a direct connection between two people who already arranged it, so
// there is no lobby list to browse and nothing to match-make.
struct NetScreen {
    char port[16] = "7777";
    char address[64] = "127.0.0.1:7777";
    char steamId[24] = ""; // a friend's SteamID, when playing over Steam
    char error[96] = "";   // last thing that went wrong, shown under the fields
};

struct NetSetupResult {
    bool back = false;
    bool host = false;
    bool join = false;
};

// `steam` swaps the screen from addresses to people: over the relay there is
// no port to forward and no IP to read out, only the SteamID a friend types in.
NetSetupResult drawNetSetup(NetScreen& s, bool steam, unsigned long long selfId) {
    NetSetupResult result;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("net-setup", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 50.0f);
    ImGui::TextUnformatted(steam ? "ONLINE - STEAM" : "ONLINE");
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 6.0f});

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 8.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.0f, 7.0f});
    // "Friend ID" is wider at 22pt than the address labels, and would sit under
    // the input box at the narrower column.
    const float labelW = steam ? 140.0f : 120.0f;
    const float fieldW = 250.0f;
    const float contentW = labelW + fieldW;

    auto row = [&](const char* label, const char* id, char* buf, std::size_t cap,
                   const char* button) {
        ImGui::PushFont(nullptr, 22.0f);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        ImGui::SameLine(labelW);
        ImGui::PushID(id);
        ImGui::PushFont(nullptr, 22.0f);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##field", buf, cap);
        // Next line, aligned under the field — not SameLine, which would put
        // the button on top of the box the player is typing in. Both this and
        // SameLine's offset are measured from the window position rather than
        // the content start, so the same labelW lines them up.
        ImGui::SetCursorPosX(labelW);
        const bool clicked = ImGui::Button(button, {fieldW, 0.0f});
        ImGui::PopFont();
        ImGui::PopID();
        return clicked;
    };

    if (steam) {
        // Nothing to type to host: your own ID *is* the address, and the
        // friend joining is the one who types.
        char mine[32];
        std::snprintf(mine, sizeof mine, "%llu", selfId);
        ImGui::PushFont(nullptr, 22.0f);
        ImGui::TextUnformatted("Your ID");
        ImGui::SameLine(labelW);
        ImGui::TextUnformatted(selfId ? mine : "(Steam not running)");
        ImGui::SetCursorPosX(labelW);
        result.host = ImGui::Button("Host a Match", {fieldW, 0.0f});
        ImGui::PopFont();
        ImGui::Dummy({0.0f, 10.0f});
        result.join =
            row("Friend ID", "steamid", s.steamId, sizeof s.steamId, "Join a Match");
    } else {
        result.host = row("Port", "port", s.port, sizeof s.port, "Host a Match");
        ImGui::Dummy({0.0f, 10.0f});
        result.join =
            row("Address", "address", s.address, sizeof s.address, "Join a Match");
    }

    ImGui::PopStyleVar(2);
    ImGui::Dummy({0.0f, 10.0f});

    // The hint line doubles as the error line: opening a socket is the thing
    // that fails here, and it should fail where the field can still be fixed.
    // Fixed-height, for the same reason drawOptions' footer is — the window is
    // centered and auto-sized, so a three-line hint collapsing to a one-line
    // error would jump the whole panel (and the Back button) up as you click.
    const bool failed = s.error[0] != '\0';
    ImGui::BeginChild("hint", {contentW, 64.0f}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleColor(ImGuiCol_Text, failed ? ImVec4{0.85f, 0.30f, 0.28f, 0.95f}
                                                : ImVec4{0.91f, 0.87f, 0.80f, 0.65f});
    ImGui::PushFont(nullptr, 17.0f);
    ImGui::TextWrapped(
        "%s", failed ? s.error
              : steam ? "Over Steam's relay - no ports, no addresses. Read your "
                        "ID out to a friend, or type in theirs. You both choose "
                        "your own fighter."
                      : "A direct connection - the same network, or a forwarded "
                        "port. The host chooses the battleground; you both choose "
                        "your own fighter.");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::Dummy({0.0f, 8.0f});

    ImGui::PushFont(nullptr, 26.0f);
    if (ImGui::Button("Back", {contentW, 0.0f})) {
        result.back = true;
    }
    ImGui::PopFont();

    ImGui::End();
    return result;
}

// Options sections, in tab order. One per `Settings` member — a new group of
// settings is a struct there plus a tab here.
enum class OptionsSection { Controls1, Controls2, Audio, Count };
constexpr int kOptionsSectionCount = static_cast<int>(OptionsSection::Count);
constexpr const char* kSectionNames[kOptionsSectionCount] = {"Controls 1", "Controls 2",
                                                             "Audio"};

// The keybind set a section edits, or null for a section that isn't controls.
Keybinds* sectionKeybinds(Settings& settings, OptionsSection s) {
    if (s == OptionsSection::Controls1) return &settings.keybinds;
    if (s == OptionsSection::Controls2) return &settings.keybinds2;
    return nullptr;
}

// Options-screen state. `capturing` is the keybind row waiting for a control;
// the two latches keep the clicks that open and close a capture from being read
// as the binding itself (see drawOptions).
struct OptionsScreen {
    OptionsSection section = OptionsSection::Controls1;
    int capturing = -1;   // Action index, or -1 while just browsing
    bool armed = false;   // a capture accepts input only once everything is up
    bool swallow = false; // ignore button clicks until the controls are up
};

struct OptionsResult {
    bool back = false;
    bool save = false;       // settings edited and settled — persist them
    bool applyAudio = false; // levels moved — push them to the mixer now
    bool previewSfx = false; // play something so a new SFX level is audible
};

// Options screen: a tab per settings section over a fixed-size content area.
// Edits land in `settings` immediately (the caller persists and applies them),
// so there's no apply step to fall out of sync with what the game is reading.
OptionsResult drawOptions(GLFWwindow* window, Settings& settings, OptionsScreen& s) {
    OptionsResult result;

    // Capture polls the bindable-control table rather than installing a GLFW
    // key callback: ImGui's GLFW backend owns those callbacks (renderer.init
    // passes install_callbacks = true), and the poll doubles as the validity
    // filter — a control the table doesn't list simply can't be bound.
    if (s.capturing >= 0) {
        Bind pressed;
        const bool anyDown = pollAnyBind(window, pressed);
        if (!s.armed) {
            // The click that opened the capture can still read as down (sticky
            // buttons hold a press until it's read once), so accept nothing
            // until every control has come up.
            s.armed = !anyDown;
        } else if (anyDown) {
            Keybinds* edited = sectionKeybinds(settings, s.section);
            const Action a = static_cast<Action>(s.capturing);
            const Bind previous = (*edited)[a];
            // A control that's already spoken for trades places with this row
            // instead of being refused: every action stays bound, so there's
            // no unbound state to render, poll, or save. The sweep covers
            // *both* sets, not just the one being edited — in a local versus
            // they are live together, so a control shared across sets would
            // move both fighters, and no amount of clicking could fix it.
            for (Keybinds* set : {&settings.keybinds, &settings.keybinds2}) {
                for (Bind& b : set->binds) {
                    if (b == pressed) {
                        b = previous;
                    }
                }
            }
            (*edited)[a] = pressed;
            s.capturing = -1;
            s.swallow = true;
            result.save = true;
        }
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("options", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 50.0f);
    ImGui::TextUnformatted("OPTIONS");
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 6.0f});

    // Rows are dense: the menu style's roomy item spacing and tall frame
    // padding would push nine keybinds off the bottom of a 720p window.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 6.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.0f, 7.0f});

    const float labelW = 190.0f;
    const float fieldW = 210.0f;
    const float contentW = labelW + fieldW;
    const char* hint = nullptr; // the hovered row's hint, drawn in the footer

    // Section tabs. The live one wears the same crimson as a hot button, so
    // "selected" and "hovered" read as the same visual language.
    const float tabW = (contentW - 10.0f) / kOptionsSectionCount;
    ImGui::PushFont(nullptr, 22.0f);
    for (int i = 0; i < kOptionsSectionCount; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 10.0f);
        }
        const bool live = static_cast<int>(s.section) == i;
        if (live) {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.09f, 0.09f, 0.85f});
        }
        const bool clicked = ImGui::Button(kSectionNames[i], {tabW, 0.0f});
        if (live) {
            ImGui::PopStyleColor();
        }
        if (clicked && !s.swallow) {
            s.section = static_cast<OptionsSection>(i);
            s.capturing = -1; // leaving the rows abandons any pending rebind
        }
    }
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 4.0f});

    // Fixed-size content area, tall enough for the longest section (the nine
    // keybind rows): the window is centered and auto-sized, so letting it
    // resize per section would jump the whole panel on every tab click.
    ImGui::BeginChild("section", {contentW, 392.0f}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (Keybinds* keys = sectionKeybinds(settings, s.section)) {
        for (int i = 0; i < kActionCount; ++i) {
            const Action a = static_cast<Action>(i);
            ImGui::PushFont(nullptr, 22.0f);
            ImGui::TextUnformatted(actionName(a));
            ImGui::PopFont();
            ImGui::SameLine(labelW);

            ImGui::PushID(i);
            const bool capturing = s.capturing == i;
            if (capturing) {
                ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.09f, 0.09f, 0.85f});
            }
            ImGui::PushFont(nullptr, 22.0f);
            const bool clicked = ImGui::Button(
                capturing ? "press a control" : bindName((*keys)[a]), {fieldW, 0.0f});
            ImGui::PopFont();
            if (capturing) {
                ImGui::PopStyleColor();
            }
            if (clicked && !s.swallow) {
                s.capturing = capturing ? -1 : i;
                s.armed = false;
            }
            if (ImGui::IsItemHovered() && s.capturing < 0) {
                hint = actionHint(a);
            }
            ImGui::PopID();
        }
    } else {
        // A level applies the moment it moves (the menu theme is playing, so
        // music is its own preview) but only persists once the drag settles —
        // otherwise one sweep across the slider is a hundred file writes.
        auto volumeRow = [&](const char* label, const char* rowHint, int id,
                             float& level, bool previewOnRelease) {
            ImGui::PushFont(nullptr, 22.0f);
            ImGui::TextUnformatted(label);
            ImGui::PopFont();
            ImGui::SameLine(labelW);

            ImGui::PushID(id);
            ImGui::PushFont(nullptr, 20.0f);
            ImGui::SetNextItemWidth(fieldW);
            float percent = level * 100.0f;
            if (ImGui::SliderFloat("##level", &percent, 0.0f, 100.0f, "%.0f%%")) {
                level = std::clamp(percent * 0.01f, 0.0f, 1.0f);
                result.applyAudio = true;
            }
            ImGui::PopFont();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                result.save = true;
                result.applyAudio = true;
                // Nothing in a menu makes an effect sound, so the SFX level
                // would otherwise be set blind.
                result.previewSfx = previewOnRelease;
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                hint = rowHint;
            }
            ImGui::PopID();
        };
        volumeRow("Music", "The menu theme and each battleground's track", 0,
                  settings.audio.music, false);
        volumeRow("Effects", "Swings, cuts, clangs, and bodies hitting the ground", 1,
                  settings.audio.sfx, true);
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::Dummy({0.0f, 8.0f});

    // Fixed-height footer: the hint changes with the hovered row, and the
    // window must not resize (and shift every row) as it does.
    ImGui::BeginChild("hint", {contentW, 58.0f}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.65f});
    ImGui::PushFont(nullptr, 17.0f);
    if (s.capturing >= 0) {
        ImGui::TextWrapped(
            "Press any key or mouse button to bind %s - Esc cancels. A control "
            "already in use trades places with this one.",
            actionName(static_cast<Action>(s.capturing)));
    } else if (hint) {
        ImGui::TextWrapped("%s", hint);
    } else {
        // Path conversion can throw on an exotic profile name; a wrong hint
        // line is not worth taking the app down for.
        static const std::string pathLabel = [] {
            try {
                return configPath().string();
            } catch (const std::exception&) {
                return std::string("bushido.toml");
            }
        }();
        ImGui::TextWrapped("%s Defaults resets this section. Saved to %s",
                           sectionKeybinds(settings, s.section)
                               ? "Click a control to rebind it."
                               : "Drag a level to set it - you hear it as it moves.",
                           pathLabel.c_str());
    }
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::EndChild();

    const float halfW = (contentW - 12.0f) * 0.5f;
    ImGui::PushFont(nullptr, 26.0f);
    // Section-scoped, so resetting the controls can't quietly move the volumes
    // (or the reverse) while the player is looking at the other tab.
    if (ImGui::Button("Defaults", {halfW, 0.0f}) && !s.swallow) {
        if (sectionKeybinds(settings, s.section)) {
            if (s.section == OptionsSection::Controls1) {
                settings.keybinds = defaultKeybinds();
            } else {
                settings.keybinds2 = defaultKeybinds2();
            }
            s.capturing = -1;
            // A section-scoped reset can re-claim a control the *other* set had
            // been moved onto, and no click on this screen would untangle that.
            // The two default sets are disjoint, so putting the other one back
            // as well always lands somewhere valid.
            bool clash = false;
            for (const Bind& a : settings.keybinds.binds) {
                for (const Bind& b : settings.keybinds2.binds) {
                    clash = clash || a == b;
                }
            }
            if (clash) {
                settings.keybinds = defaultKeybinds();
                settings.keybinds2 = defaultKeybinds2();
            }
        } else {
            settings.audio = AudioSettings{};
            result.applyAudio = true;
        }
        result.save = true;
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button("Back", {halfW, 0.0f}) && !s.swallow) {
        result.back = true;
    }
    ImGui::PopFont();

    ImGui::End();

    // Cleared *after* the widgets: ImGui fires a button on mouse release, which
    // is the same frame the swallow would otherwise go false — so the release
    // that completed a capture would re-open it.
    if (s.swallow) {
        Bind ignored;
        s.swallow = pollAnyBind(window, ignored);
    }
    return result;
}

// Five-segment rating bar (select screen). Draws with the low-level draw
// list; ImGui only reserves the space.
void drawStatBar(const char* label, int rating, ImU32 fill) {
    ImGui::PushFont(nullptr, 17.0f);
    ImGui::TextUnformatted(label);
    ImGui::PopFont();
    ImGui::SameLine(78.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    const float segW = 20.0f, segH = 11.0f, gap = 4.0f;
    for (int s = 0; s < 5; ++s) {
        ImVec2 mn{at.x + s * (segW + gap), at.y + 4.0f};
        ImVec2 mx{mn.x + segW, mn.y + segH};
        if (s < rating) {
            dl->AddRectFilled(mn, mx, fill, 2.0f);
        } else {
            dl->AddRect(mn, mx, IM_COL32(200, 190, 170, 60), 2.0f);
        }
    }
    ImGui::Dummy({5 * (segW + gap), segH + 6.0f});
}

// One select tile: a colored button face with luminance-aware name text and,
// when previewed, the crimson frame. Returns true on click.
bool drawSelectTile(const char* name, const glm::vec4& face, float tile,
                    bool previewed) {
    // Tile face brightens on hover.
    ImVec4 base{face.r, face.g, face.b, 0.75f};
    ImVec4 hot{base.x * 1.25f + 0.05f, base.y * 1.25f + 0.05f,
               base.z * 1.25f + 0.05f, 0.9f};
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hot);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hot);
    // Dark name text on bright tiles (Kensei's undyed kimono, the wakizashi's
    // pale steel), cream on the rest.
    float lum = 0.2126f * base.x + 0.7152f * base.y + 0.0722f * base.z;
    ImGui::PushStyleColor(ImGuiCol_Text, lum > 0.45f
                                             ? ImVec4{0.15f, 0.13f, 0.11f, 1.0f}
                                             : ImVec4{0.91f, 0.87f, 0.80f, 1.0f});
    ImGui::PushFont(nullptr, 26.0f);
    bool clicked = ImGui::Button(name, {tile, tile});
    ImGui::PopFont();
    ImGui::PopStyleColor(4);

    if (previewed) {
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(
            {mn.x + 3.0f, mn.y + 3.0f}, {mx.x - 3.0f, mx.y - 3.0f},
            IM_COL32(214, 44, 44, 255), 2.0f, 0, 2.5f);
    }
    return clicked;
}

// Single-player select, three stages on one screen, all mouse-driven: a row
// of fighter tiles, then (once one is clicked) a row of weapon tiles, then a
// row of battleground tiles. Hovering a tile previews it in the panel below;
// clicking a level locks everything and starts the match (the caller draws
// the opponent — fighter and blade — at random). Returns character -1 while
// still browsing.
SelectResult drawCharacterSelect(SelectScreen& s) {
    SelectResult picked;
    const bool weaponStage = s.pickedCharacter >= 0 && s.pickedWeapon < 0;
    const bool levelStage = s.pickedWeapon >= 0 && s.pickLevel;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("char-select", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    const char* stage = levelStage    ? "CHOOSE YOUR BATTLEGROUND"
                        : weaponStage ? "CHOOSE YOUR BLADE"
                                      : "CHOOSE YOUR FIGHTER";
    ImGui::PushFont(nullptr, 50.0f);
    if (s.mode == SetupMode::LocalVersus) {
        // Only local versus needs the number: it is the one mode where two
        // people take turns at the same screen.
        ImGui::Text("PLAYER %d - %s", s.player + 1, stage);
    } else {
        ImGui::TextUnformatted(stage);
    }
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 6.0f});

    const float tile = 132.0f;
    const float tileGap = 14.0f;
    // The panel keeps the fighter row's width in both stages so the window
    // (and the title above it) doesn't jump when the rows swap.
    const float panelW = kCharacterCount * tile + (kCharacterCount - 1) * tileGap;

    if (levelStage) {
        for (int i = 0; i < kLevelCount; ++i) {
            const LevelDef& l = levelDef(i);
            if (i > 0) {
                ImGui::SameLine(0.0f, tileGap);
            }
            ImGui::PushID(i);
            if (drawSelectTile(l.name, l.tileColor, tile, s.shownLevel == i)) {
                picked = {s.pickedCharacter, s.pickedWeapon, i};
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                s.shownLevel = i;
            }
        }
    } else if (weaponStage) {
        for (int i = 0; i < kWeaponCount; ++i) {
            const WeaponDef& w = weaponDef(i);
            if (i > 0) {
                ImGui::SameLine(0.0f, tileGap);
            }
            ImGui::PushID(i);
            if (drawSelectTile(w.name, w.tileColor, tile, s.shownWeapon == i)) {
                s.pickedWeapon = i;
                if (!s.pickLevel) {
                    // The ground is already settled (the other player chose
                    // it), so the blade is this one's last click.
                    picked = {s.pickedCharacter, i, -1};
                }
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                s.shownWeapon = i;
            }
        }
    } else {
        for (int i = 0; i < kCharacterCount; ++i) {
            const CharacterDef& c = characterDef(i);
            if (i > 0) {
                ImGui::SameLine(0.0f, tileGap);
            }
            ImGui::PushID(i);
            if (drawSelectTile(c.name, c.colors.kimono, tile, s.shown == i)) {
                s.pickedCharacter = i;
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                s.shown = i;
            }
        }
    }
    ImGui::Dummy({0.0f, 10.0f});

    // Detail panel for the previewed tile: name, epithet, stat bars. The menu
    // style's roomy 16px item spacing would push the lower rows out of the
    // fixed-height panel; tighten it locally.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 6.0f});
    const ImU32 kFill = IM_COL32(214, 44, 44, 255);
    ImGui::BeginChild("stats", {panelW, 200.0f}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    const char* name;
    const char* epithet;
    if (levelStage) {
        const LevelDef& l = levelDef(s.shownLevel);
        name = l.name;
        epithet = l.epithet;
    } else if (weaponStage) {
        const WeaponDef& w = weaponDef(s.shownWeapon);
        name = w.name;
        epithet = w.epithet;
    } else {
        const CharacterDef& c = characterDef(s.shown);
        name = c.name;
        epithet = c.epithet;
    }
    ImGui::PushFont(nullptr, 30.0f);
    if (levelStage) {
        // Keep the whole locked loadout on screen while browsing grounds.
        ImGui::Text("%s  -  %s  -  %s", characterDef(s.pickedCharacter).name,
                    weaponDef(s.pickedWeapon).name, name);
    } else if (weaponStage) {
        // Keep the chosen fighter on screen while browsing blades.
        ImGui::Text("%s  -  %s", characterDef(s.pickedCharacter).name, name);
    } else {
        ImGui::TextUnformatted(name);
    }
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.65f});
    ImGui::PushFont(nullptr, 17.0f);
    ImGui::TextUnformatted(epithet);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Dummy({0.0f, 2.0f});
    if (levelStage) {
        // Levels have no stats — the epithet carries the preview.
    } else if (weaponStage) {
        const WeaponDef& w = weaponDef(s.shownWeapon);
        drawStatBar("Speed", w.rSpeed, kFill);
        drawStatBar("Damage", w.rDamage, kFill);
        drawStatBar("Reach", w.rReach, kFill);
    } else {
        const CharacterDef& c = characterDef(s.shown);
        drawStatBar("Speed", c.rSpeed, kFill);
        drawStatBar("Power", c.rPower, kFill);
        drawStatBar("Reach", c.rReach, kFill);
        drawStatBar("Weight", c.rWeight, kFill);
    }
    ImGui::Dummy({0.0f, 2.0f});
    ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.45f});
    ImGui::PushFont(nullptr, 17.0f);
    // What the next click actually does, which differs per mode — a netplay
    // host's opponent is not drawn at random, they are a person still picking.
    const bool net =
        s.mode == SetupMode::NetHost || s.mode == SetupMode::NetClient;
    const char* hint;
    if (levelStage) {
        hint = s.mode == SetupMode::LocalVersus
                   ? "Click a battleground to hand over to player 2"
               : net ? "Click a battleground, then wait for your opponent"
                     : "Click a battleground to begin - your opponent is drawn at random";
    } else if (weaponStage) {
        // A netplay peer that does not own the battleground finishes on the
        // blade — either because it is joining, or because it won the last one.
        hint = s.pickLevel ? "Click a blade to choose the battleground"
               : net       ? "Click a blade, then wait for your opponent"
                           : "Click a blade to enter the arena";
    } else {
        hint = "Click a fighter to choose their blade";
    }
    ImGui::TextUnformatted(hint);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    return picked;
}

// In-match HUD: a blood bar per fighter in the top corners, draining toward
// the outside as they take cuts (or bleed out from stumps). Drawn straight to
// the foreground draw list — no window, so it never steals the mouse.
void drawBloodBars(const Game& game) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = std::min(420.0f, vp->Size.x * 0.34f);
    const float h = 18.0f, pad = 24.0f, top = 20.0f;
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        const float frac = p.blood / Player::kMaxBlood;
        const float x0 = i == 0 ? pad : vp->Size.x - pad - w;
        const ImVec2 mn{x0, top}, mx{x0 + w, top + h};
        dl->AddRectFilled(mn, mx, IM_COL32(0, 0, 0, 130), 3.0f);
        if (frac > 0.0f) {
            // Player 1's bar is anchored left, player 2's mirrored right.
            ImVec2 fmn = i == 0 ? mn : ImVec2{mx.x - w * frac, top};
            ImVec2 fmx = i == 0 ? ImVec2{x0 + w * frac, mx.y} : mx;
            dl->AddRectFilled(fmn, fmx, IM_COL32(214, 44, 44, 220), 3.0f);
        }
        dl->AddRect(mn, mx, IM_COL32(232, 222, 204, 70), 3.0f, 0, 1.5f);

        char label[64];
        std::snprintf(label, sizeof(label), "%s - %s", game.character(i).name,
                      game.weapon(i).name);
        ImFont* font = ImGui::GetFont();
        const float nameSize = 19.0f;
        const float nameW = font->CalcTextSizeA(nameSize, 1e9f, 0.0f, label).x;
        dl->AddText(font, nameSize, {i == 0 ? x0 : mx.x - nameW, mx.y + 6.0f},
                    IM_COL32(232, 222, 204, 200), label);
    }
}

// Post-match overlay, styled after the main menu. Shown once the kill has had
// a moment to play out; Rematch reruns the same pairing with a fresh Game.
enum class OverAction { None, Rematch, Select, Disconnect };

// `localSlot` is the fighter this machine's player drives — 0 everywhere
// except a netplay client, which drives fighter 2. Across a wire a rematch is
// a request rather than a restart, so `asked` swaps the button for the wait.
OverAction drawWinOverlay(const Game& game, bool versus, int localSlot, bool net,
                          bool asked) {
    OverAction action = OverAction::None;
    // Against the bot (or across a wire) the verdict is the reader's own; with
    // two humans at one keyboard, "VICTORY" would be true for exactly one of
    // the people looking at the screen.
    const char* title = versus ? (game.winner() == 0 ? "PLAYER 1 WINS" : "PLAYER 2 WINS")
                               : (game.winner() == localSlot ? "VICTORY" : "SLAIN");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("match-over", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 88.0f);
    const float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.65f});
    ImGui::PushFont(nullptr, 22.0f);
    ImGui::Text("%s takes the duel", game.character(game.winner()).name);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Dummy({0.0f, 12.0f});

    const float buttonWidth = 240.0f;
    const float buttonX = ImGui::GetStyle().WindowPadding.x +
                          (std::max(titleWidth, buttonWidth) - buttonWidth) * 0.5f;
    ImGui::PushFont(nullptr, 30.0f);
    if (net) {
        ImGui::SetCursorPosX(buttonX);
        if (asked) {
            // Asked and waiting: the other player has to want it too, so there
            // is nothing to click but out.
            ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.65f});
            ImGui::PushFont(nullptr, 22.0f);
            ImGui::TextUnformatted("waiting for your opponent...");
            ImGui::PopFont();
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Rematch", {buttonWidth, 0.0f})) {
                action = OverAction::Rematch;
            }
            ImGui::SetCursorPosX(buttonX);
            // Changing fighters keeps the connection: the alternative is
            // disconnecting and redoing the whole host-and-join dance to swap
            // a sword.
            if (ImGui::Button("Change Fighters", {buttonWidth, 0.0f})) {
                action = OverAction::Select;
            }
        }
        ImGui::SetCursorPosX(buttonX);
        if (ImGui::Button("Disconnect", {buttonWidth, 0.0f})) {
            action = OverAction::Disconnect;
        }
    } else {
        ImGui::SetCursorPosX(buttonX);
        if (ImGui::Button("Rematch", {buttonWidth, 0.0f})) {
            action = OverAction::Rematch;
        }
        ImGui::SetCursorPosX(buttonX);
        if (ImGui::Button("Character Select", {buttonWidth, 0.0f})) {
            action = OverAction::Select;
        }
    }
    ImGui::PopFont();

    ImGui::End();
    return action;
}

// Netplay status line, over the arena. Drawn whenever the session isn't
// quietly running, so a stall waiting on the opponent's input reads as what it
// is rather than as the game having hung.
void drawNetBanner(const Session& session) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = 24.0f;
    const char* text = session.status();
    const float w = font->CalcTextSizeA(size, 1e9f, 0.0f, text).x;
    const ImVec2 at{(vp->Size.x - w) * 0.5f, vp->Size.y * 0.5f - 130.0f};
    dl->AddRectFilled({at.x - 18.0f, at.y - 9.0f}, {at.x + w + 18.0f, at.y + size + 9.0f},
                      IM_COL32(0, 0, 0, 150), 4.0f);
    dl->AddText(font, size, at, IM_COL32(232, 222, 204, 230), text);
}

// Connection readout, top centre between the blood bars. The numbers existed
// before this but only in a line printed at exit, which is no use at all to
// the person wondering mid-match why their swings feel late — a link short of
// buffer shows up as the whole fight running thick, and nothing on screen
// said so. Colour carries the verdict; the numbers say why.
void drawNetStatus(const Session& session) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = 17.0f;
    const float rate = session.stallRate();

    char text[96];
    std::snprintf(text, sizeof text, "%.0f ms   delay %df%s", session.rttMs(),
                  session.inputDelay(), rate > 0.25f ? "   STALLING" : "");

    ImU32 color = IM_COL32(232, 222, 204, 130);      // fine
    if (rate > 0.25f) {
        color = IM_COL32(214, 44, 44, 235);          // the match is being held up
    } else if (rate > 0.05f) {
        color = IM_COL32(224, 176, 72, 200);         // ragged but playable
    }
    const float w = font->CalcTextSizeA(size, 1e9f, 0.0f, text).x;
    dl->AddText(font, size, {(vp->Size.x - w) * 0.5f, 24.0f}, color, text);
}

void drawBox(Renderer& renderer, glm::vec3 center, glm::vec3 size, glm::vec4 color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);
    renderer.drawBox(model, color);
}

void drawScene(Renderer& renderer, const Game& game, float time) {
    // Level scenery (floor, backdrop, ambient animation) goes down first so
    // everything gameplay draws sits on top of it. The game knows its own
    // battleground, which keeps the drawn scenery and the sim's obstacle
    // colliders on the same level by construction.
    drawLevel(renderer, game.level(), time);

    // Blood splats staining the floor. Each mark carries its own y jitter and
    // yaw so overlaps don't z-fight or look stamped from the same die.
    for (const BloodMark& mark : game.bloodMarks()) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), mark.pos);
        model = glm::rotate(model, mark.yaw, {0.0f, 1.0f, 0.0f});
        model = glm::scale(model, {mark.radius * 2.0f, 0.012f, mark.radius * 1.3f});
        renderer.drawBox(model, {0.32f, 0.02f, 0.02f, mark.alpha});
    }

    // Blob shadows: without these, depth position is unreadable while airborne.
    // A toppled body stretches its shadow toward the side it lies on.
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        float lieZ = p.facing * std::sin(p.bodyRoll());
        drawBox(renderer, {p.pos.x, 0.03f, p.pos.z + lieZ * 0.8f},
                {Player::kHalfWidth * 2.2f, 0.02f,
                 Player::kHalfWidth * 1.6f + std::abs(lieZ) * 1.4f},
                {0.0f, 0.0f, 0.0f, 0.45f});
    }

    // Fighters, in their chosen character's colors; the blade's length is the
    // resolved reach (character + weapon) and its thickness the weapon's.
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        float yaw = p.facing > 0.0f ? 0.0f : 3.14159265358979f;
        glm::vec3 feet{p.pos.x, p.pos.y - Player::kHalfHeight, p.pos.z};
        SamuraiColors c = game.character(i).colors;
        if (p.hitstun > 0.0f) {
            c.kimono = glm::mix(c.kimono, glm::vec4(1.0f), 0.7f * p.hitstun / 0.35f);
        }
        // The riposte window glows gold — strike now; fades as it closes.
        if (p.riposteTime > 0.0f) {
            c.kimono = glm::mix(c.kimono, glm::vec4{0.95f, 0.78f, 0.30f, 1.0f},
                                0.55f * std::min(1.0f, p.riposteTime / 0.25f));
        }
        drawSamurai(renderer, feet, yaw,
                    {p.animPhase, p.moveAmount, p.grounded, time,
                     static_cast<int>(p.attackState), static_cast<int>(p.attackKind),
                     p.attackT, p.blocking, p.crouchAmount, game.stats(i).reach,
                     game.weapon(i).stats.bladeWidth, p.bodyRoll(), p.severed},
                    c);
    }

    // Severed limbs tumbling as physics debris, in their owner's colors.
    for (const SeveredPiece& piece : game.severedPieces()) {
        drawSeveredLimb(renderer, game.severedPieceTransform(piece),
                        static_cast<int>(piece.limb),
                        game.character(piece.victim).colors);
    }

    // Blood droplets in flight.
    for (const BloodParticle& drop : game.bloodParticles()) {
        drawBox(renderer, drop.pos, glm::vec3{drop.size}, {0.48f, 0.03f, 0.03f, 1.0f});
    }
}

} // namespace

int main(int argc, char** argv) {
    // Developer flags, all three for the determinism harness (netplay/replay.hpp).
    //   --record <file>  write every match played this run to <file>
    //   --replay <file>  skip the menus, re-run the recorded match, check it
    //   --auto           skip the menus, play a fixed pairing, quit when it ends
    // And two for netplay (src/netplay/session.hpp), which is direct-IP only —
    // LAN or a forwarded port, no NAT traversal:
    //   --host <port>          pick a match as usual, then wait for an opponent
    //   --join <host:port>     skip the menus, take the match the host sends
    //   --steam                play over Steam's relay instead of direct IP
    //   --netsim <l[,j[,loss]]>  imitate a worse link (ms, ms, percent)
    //   --delay <frames>       pin the input delay; default is to measure it
    // --auto is what makes recording scriptable: nobody has to click through a
    // select screen, and the bot supplies a whole match of hits, dismemberment
    // and debris against a player who never moves.
    //   bushido --record run.bsdr --auto   then   bushido --replay run.bsdr
    const char* recordPath = nullptr;
    const char* replayPath = nullptr;
    const char* joinTarget = nullptr;
    bool autoMatch = false;
    bool netHost = false;
    std::uint16_t hostPort = 0;
    bool netSim = false;
    bool steamMode = false;
    SimulatedLink::Conditions simConditions;
    int inputDelay = 0; // 0 = measure the link and choose; --delay pins it
    for (int i = 1; i < argc; ++i) {
        bool wantsArg = std::strcmp(argv[i], "--record") == 0 ||
                        std::strcmp(argv[i], "--replay") == 0 ||
                        std::strcmp(argv[i], "--host") == 0 ||
                        std::strcmp(argv[i], "--netsim") == 0 ||
                        std::strcmp(argv[i], "--delay") == 0 ||
                        std::strcmp(argv[i], "--join") == 0;
        if (wantsArg && i + 1 >= argc) {
            std::fprintf(stderr, "%s needs an argument\n", argv[i]);
            return 1;
        }
        if (std::strcmp(argv[i], "--record") == 0) {
            recordPath = argv[++i];
        } else if (std::strcmp(argv[i], "--replay") == 0) {
            replayPath = argv[++i];
        } else if (std::strcmp(argv[i], "--auto") == 0) {
            autoMatch = true;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            long port = std::strtol(argv[++i], nullptr, 10);
            if (port <= 0 || port > 65535) {
                std::fprintf(stderr, "--host needs a port between 1 and 65535\n");
                return 1;
            }
            netHost = true;
            hostPort = static_cast<std::uint16_t>(port);
        } else if (std::strcmp(argv[i], "--join") == 0) {
            joinTarget = argv[++i];
        } else if (std::strcmp(argv[i], "--steam") == 0) {
            steamMode = true;
        } else if (std::strcmp(argv[i], "--netsim") == 0) {
            if (!parseConditions(argv[++i], simConditions)) {
                std::fprintf(stderr, "--netsim wants latencyMs[,jitterMs[,loss%%]], "
                                     "e.g. 40,8,1.5\n");
                return 1;
            }
            netSim = true;
        } else if (std::strcmp(argv[i], "--delay") == 0) {
            long frames = std::strtol(argv[++i], nullptr, 10);
            if (frames < 1 || frames > kMaxInputDelay) {
                std::fprintf(stderr, "--delay wants 1..%d frames\n", kMaxInputDelay);
                return 1;
            }
            inputDelay = static_cast<int>(frames);
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    if (recordPath && replayPath) {
        std::fprintf(stderr, "--record and --replay are mutually exclusive\n");
        return 1;
    }
    if (replayPath && autoMatch) {
        std::fprintf(stderr, "--replay already runs one match and quits; drop --auto\n");
        return 1;
    }
    if (netHost && joinTarget) {
        std::fprintf(stderr, "--host and --join are mutually exclusive\n");
        return 1;
    }
    if ((netHost || joinTarget) && replayPath) {
        std::fprintf(stderr, "netplay cannot be combined with --replay\n");
        return 1;
    }
    // --auto alongside netplay means "let the bot drive my fighter", which is
    // sound rather than a hack: the bot is an input source like any other, its
    // choices are scheduled and sent exactly as a human's would be, and the
    // peer replays them without knowing the difference. It is also the only
    // way to soak-test the protocol without two people.
    // Resolved up front so a typo fails before a window opens.
    std::string joinHost;
    std::uint16_t joinPort = 0;
    // Under --steam the target is a person, not an endpoint, so there is
    // nothing here to resolve.
    if (joinTarget && !steamMode && !parseEndpoint(joinTarget, joinHost, joinPort)) {
        std::fprintf(stderr, "--join wants host:port, e.g. 192.168.1.20:7777\n");
        return 1;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "bushido", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    Renderer renderer;
    try {
        renderer.init(window);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "renderer init failed: %s\n", e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Sticky input: a press+release that both arrive between two polls (very
    // fast taps) still reads as PRESS once, so button edges are never dropped.
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);

    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        static_cast<Renderer*>(glfwGetWindowUserPointer(w))->onResize();
    });

    setupMenuStyle();

    Audio audio; // logs and stays silent if no device; the game runs regardless
    // The arena diorama behind the menu, built by the last load step; every
    // lock-in on the select screen replaces it with a fresh match. Null until
    // then, which is why the loading branch of the loop returns early.
    std::unique_ptr<Game> game;
    Bot bot; // drives player 2; re-seeded per match
    FramingCamera camera;
    std::uint32_t sfxSeed = 0xb0051d0u; // pitch-jitter rng
    AppState state = AppState::Loading;
    SelectScreen select;
    // Player settings, from %APPDATA%/bushido/bushido.toml if it's there; a
    // missing or broken file just leaves the defaults standing (loadConfig says
    // which). The mixer levels have to be pushed to Audio; the keybinds are
    // read straight out of `settings` every frame.
    Settings settings;
    OptionsScreen options;

    // Everything that has to happen before the menu can come up, one step per
    // frame. Nothing here is busywork padding a bar: the audio steps open the
    // device, synthesize the effects and load the theme the menu comes up
    // playing, and the last two pull one-time costs out of the first match.
    const LoadStep loadSteps[] = {
        {"Opening the hall", 25.0f, [&] { audio.initDevice(); }},
        {"Reading your settings", 0.5f,
         [&] {
             loadConfig(settings);
             // Safe this early: the levels are stored either way, and each
             // track picks the music one up as it loads — the menu's a step
             // down, a battleground's not until its match.
             audio.setMusicVolume(settings.audio.music);
             audio.setSfxVolume(settings.audio.sfx);
         }},
        {"Forging the blades", 3.0f, [&] { audio.initSfx(); }},
        // Only the front-end theme: a battleground's is loaded with its match,
        // where one decode is lost in the noise of building the arena.
        {"Tuning the koto", 4.5f, [&] { audio.loadMusic(Music::Menu); }},
        {"Taking up the voices", 0.05f, [&] { audio.initVoices(); }},
        {"Raising the battlegrounds", 0.5f,
         [&] {
             // Each level's static geometry is generated once, on the first
             // ask, and cached — asking now moves that hitch off the first
             // match onto the bar, where it's expected.
             for (int i = 0; i < kLevelCount; ++i) {
                 levelObstacles(i);
                 levelGround(i);
             }
         }},
        {"Squaring the arena", 3.0f,
         [&] { game = std::make_unique<Game>(0, 0, 1, 0, 0); }}, // boots Jolt
    };
    const int loadStepCount = static_cast<int>(std::size(loadSteps));
    int loadStep = 0;      // the next step to run
    float loadDone = 0.0f; // weight completed so far
    float loadTotal = 0.0f;
    for (const LoadStep& s : loadSteps) {
        loadTotal += s.weight;
    }
    float loadShown = 0.0f; // the drawn bar, easing toward loadDone/loadTotal
    // Opponent draw on the select screen, and the seed handed to each new Bot.
    // --auto is a fixture rather than a play session, so it seeds from a
    // constant: two auto runs then play the identical match and their
    // recordings compare byte for byte, which checks the whole pipeline from
    // construction outward rather than just the replay of one run.
    std::mt19937 rng{autoMatch ? 0x5eed0f21u : std::random_device{}()};
    // The current match's loadout — (character, weapon) roster indices per
    // player — kept outside Game so Rematch can rebuild the same pairing.
    int matchChars[2] = {0, 1};
    int matchWeapons[2] = {0, 0};
    // The battleground index rides beside the loadout so Rematch keeps the
    // arena; Game bakes it in at construction (scenery + obstacle colliders).
    int matchLevel = 0;
    // Who drives each fighter. Rides with the loadout for the same reason —
    // Rematch keeps it — and is the one place a local-versus or netplay match
    // would differ from this one.
    InputSource matchSources[2] = {InputSource::Local0, InputSource::Bot};
    // Netplay. The transport is a plain UDP socket; the session is the
    // lockstep protocol over it (netplay/session.hpp). Both stay Idle unless
    // --host or --join was passed.
    // Declared here rather than beside the accumulator because the netplay
    // session needs it too — it is what turns a round trip in milliseconds
    // into an input delay in frames.
    constexpr float kFixedDt = 1.0f / 120.0f;

    UdpTransport transport;
    SteamTransport steamTransport;
    // --steam swaps direct IP for Steam's relay. Opt-in rather than the
    // default because it cannot be tested from one machine — one Steam client
    // is one person, so two peers here would both be you.
    if (steamMode && !steamTransport.start()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1; // it said why
    }
    Transport& baseLink =
        steamMode ? static_cast<Transport&>(steamTransport) : transport;
    // --netsim slips a deliberately worse link in front of whichever it is. The
    // session only ever sees a Transport, which is the point of the interface.
    std::unique_ptr<SimulatedLink> simLink;
    if (netSim) {
        simLink = std::make_unique<SimulatedLink>(baseLink, simConditions, 0xa17e51u);
    }
    Transport* netLink = simLink ? static_cast<Transport*>(simLink.get()) : &baseLink;
    Session session;
    // Local versus sets a match up in two passes — one human picks, hands the
    // keyboard over, the other picks — so the flow has to remember which of
    // them is at the screen and whether a second pass is still owed.
    SetupMode setupMode = SetupMode::Solo;
    int selectingPlayer = 0;
    NetScreen netScreen;
    // True while a select screen is re-picking into an *existing* session
    // rather than setting one up, which is the difference between submitting a
    // loadout over the wire and opening a fresh handshake.
    bool netReselect = false;
    // Whether this peer's battleground pick is the one that counts next match.
    bool netOwnsLevel = false;
    // Opens one pass of the select screen. `chooseLevel` is false for whoever
    // picks second into a ground someone else already settled: local versus's
    // player 2, and a netplay client, whose host owns the battleground. Their
    // blade click is therefore the one that ends the pass.
    auto beginSelect = [&](SetupMode mode, int player, bool chooseLevel) {
        setupMode = mode;
        selectingPlayer = player;
        select = SelectScreen{};
        select.mode = mode;
        select.player = player;
        select.pickLevel = chooseLevel;
    };
    // Hands this machine's own half of the match to the session. The other
    // half arrives over the handshake — the host learns the client's fighter
    // from its Hello, the client learns the whole match from the Welcome — so
    // neither player has a fighter chosen for them.
    auto beginNetSession = [&](Session::Role role) {
        MatchSetup mine;
        for (int i = 0; i < 2; ++i) {
            mine.chars[i] = matchChars[i];
            mine.weapons[i] = matchWeapons[i];
        }
        mine.level = matchLevel;
        session.start(netLink, role, mine, inputDelay, kFixedDt);
    };

    float accumulator = 0.0f;
    float elapsed = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    // One latch per local control set (see input.hpp): the held controls are
    // read per render frame, the attack edges wait here for a fixed step.
    LocalInput localInputs[2];
    // Which control set a local slot reads: [keybinds] and [keybinds_p2] from
    // the config, kept disjoint by the options screen since in a versus both
    // are live at once.
    auto localKeys = [&](int slot) -> const Keybinds& {
        return slot == 0 ? settings.keybinds : settings.keybinds2;
    };
    bool escHeld = false;

    // The determinism harness (--record / --replay). Both are inert unless the
    // matching flag was passed. Replaying drives the sim from the log instead
    // of from matchSources, and checks the sim's checksum every step.
    Recorder recorder;
    Replayer replayer;
    bool desynced = false;
    bool warnedLossy = false;

    // Fresh match from matchChars. Seeding the button latches from the live
    // state keeps the click that started the match from reading as an attack:
    // a held LMB is flagged so even its eventual release swings nothing.
    auto startMatch = [&] {
        // The battleground's theme, decoded now rather than at startup — the
        // level is what says which track is wanted, and this is the frame
        // that's already paying for a new arena. A no-op on a rematch, or on
        // any level whose track has been in memory since an earlier match.
        audio.loadMusic(levelMusic(matchLevel));
        game = std::make_unique<Game>(matchChars[0], matchWeapons[0], matchChars[1],
                                      matchWeapons[1], matchLevel);
        state = AppState::Playing;
        for (int i = 0; i < 2; ++i) {
            if (matchSources[i] == InputSource::Bot) {
                bot = Bot{i, rng()}; // fresh brain (and rng stream) each match
            }
            int slot = localSlot(matchSources[i]);
            if (slot >= 0) {
                localInputs[slot].beginMatch(window, localKeys(slot));
            }
        }
        // A recording covers one match; starting another (or a rematch)
        // replaces it, so what's on disk is always the match just played.
        if (recordPath) {
            MatchSetup setup;
            for (int i = 0; i < 2; ++i) {
                setup.chars[i] = matchChars[i];
                setup.weapons[i] = matchWeapons[i];
            }
            setup.level = matchLevel;
            recorder.open(recordPath, setup);
        }
    };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Esc backs out one level: battleground stage -> blade stage ->
        // fighter stage, match/select -> menu, menu -> quit.
        bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !escHeld) {
            if (state == AppState::Loading) {
                // Nothing to back out to, and the menu can't be entered
                // half-built — Esc during the load just leaves.
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else if (state == AppState::Options && options.capturing >= 0) {
                options.capturing = -1; // cancel the rebind, stay on the screen
            } else if (state == AppState::CharacterSelect && select.pickedWeapon >= 0) {
                select.pickedWeapon = -1;
            } else if (state == AppState::CharacterSelect && select.pickedCharacter >= 0) {
                select.pickedCharacter = -1;
            } else if (state != AppState::Menu) {
                // Backing out of a netplay match drops the connection: the
                // peer's timeout is what tells them, since there is nothing
                // useful to say once this side has stopped stepping.
                session.stop();
                state = AppState::Menu;
            } else {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        escHeld = escDown;

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        elapsed += frameTime;

        if (state == AppState::Loading) {
            // The bar eases toward the completed fraction instead of snapping
            // to it: nine steps of uneven cost would otherwise jump the fill in
            // visible chunks. It only ever chases real completions, so it can't
            // run ahead of the work — and on a fast machine the whole load
            // reads as one sweep rather than a single frame at 100%.
            const float target = loadDone / loadTotal;
            loadShown += (target - loadShown) * (1.0f - std::exp(-9.0f * frameTime));

            const bool stepsLeft = loadStep < loadStepCount;
            if (renderer.beginFrame()) {
                drawLoadingScreen(loadShown,
                                  stepsLeft ? loadSteps[loadStep].label : "Ready");
                renderer.endFrame();
            }
            // The step runs *after* the frame naming it is on screen, so the
            // label the player is reading is the work they're waiting on.
            if (stepsLeft) {
                loadSteps[loadStep].run();
                loadDone += loadSteps[loadStep].weight;
                ++loadStep;
            } else if (loadShown > 0.995f) {
                // --replay skips the front end: the log names the match, so
                // there is nothing left to pick.
                const bool netFlag = netHost || joinTarget;
                // Same two shortcuts either way; over Steam --join names a
                // person, which is what the two-machine test will use.
                const bool socketOpen =
                    !netFlag ? true
                    : steamMode
                        ? (netHost ? steamTransport.listen()
                                   : steamTransport.connectTo(
                                         std::strtoull(joinTarget, nullptr, 10)))
                        : (joinTarget ? transport.join(joinHost.c_str(), joinPort)
                                      : transport.host(hostPort));
                if (netFlag && !socketOpen) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE); // it logged why
                } else if (netFlag && autoMatch) {
                    // Hands-off netplay soak run: nobody picks, so the pairing
                    // is the fixture's and the bot drives this side.
                    matchLevel = kLevelCount > 1 ? 1 : 0;
                    beginNetSession(netHost ? Session::Role::Host
                                            : Session::Role::Client);
                    state = AppState::NetWait;
                } else if (netFlag) {
                    // The flags are a shortcut past the Online screen, not past
                    // the picking — both players still choose their own fighter.
                    beginSelect(netHost ? SetupMode::NetHost : SetupMode::NetClient,
                                netHost ? 0 : 1, netHost);
                    state = AppState::CharacterSelect;
                } else if (!replayPath && autoMatch) {
                    // Nobody picks, so the pairing is fixed. Hanami rather
                    // than Dojo: carved ground, solid stones and water mean
                    // strictly more physics for two runs to disagree about.
                    matchLevel = kLevelCount > 1 ? 1 : 0;
                    startMatch();
                } else if (!replayPath) {
                    state = AppState::Menu;
                } else if (!replayer.open(replayPath)) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE); // it logged why
                } else {
                    const MatchSetup& setup = replayer.setup();
                    if (!setupIsSane(setup)) {
                        std::fprintf(stderr,
                                     "replay: setup names a fighter, blade or "
                                     "battleground this build does not have\n");
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                    } else {
                        for (int i = 0; i < 2; ++i) {
                            matchChars[i] = setup.chars[i];
                            matchWeapons[i] = setup.weapons[i];
                        }
                        matchLevel = setup.level;
                        startMatch();
                    }
                }
            }
            continue; // nothing below this exists yet
        }

        // Netplay runs on the render frame, not the fixed step: packets have to
        // keep flowing during a stall — ours are exactly what the peer is
        // waiting for — and a stall means no fixed step runs at all.
        // Steam's callbacks only fire from RunCallbacks, and one of them is how
        // a host learns someone is calling — so it ticks from the moment the
        // Online screen is up, not just once a session exists.
        if (steamMode) {
            steamTransport.pump();
        }
        if (session.active()) {
            if (simLink) {
                simLink->advance(frameTime); // nothing is released until it ticks
            }
            session.pump(frameTime);
        }

        // The handshake landed: the host's match description is in hand, so
        // this machine can finally build the same Game the host did.
        // Re-picking into a live session: both halves are in when each peer has
        // said it is ready — or when the peer has already rolled forward, which
        // is the same race the rematch flag has and is closed the same way.
        if (state == AppState::NetWait && netReselect &&
            (session.loadoutsExchanged() ||
             session.agreedIntent() == Session::Intent::Rematch)) {
            netReselect = false;
            session.beginRematch(); // merges their half in, rolls the match on
            const MatchSetup& merged = session.setup();
            if (!setupIsSane(merged)) {
                std::fprintf(stderr, "net: the opponent picked a fighter, blade or "
                                     "battleground this build does not have\n");
                session.stop();
                state = AppState::Menu;
            } else {
                for (int i = 0; i < 2; ++i) {
                    matchChars[i] = merged.chars[i];
                    matchWeapons[i] = merged.weapons[i];
                }
                matchLevel = merged.level;
                startMatch();
            }
        }

        if (state == AppState::NetWait && !netReselect &&
            session.state() == Session::State::Running) {
            const MatchSetup& setup = session.setup();
            if (!setupIsSane(setup)) {
                // Both roles check: a client is trusting the host's whole
                // match, and a host is trusting the fighter the client sent.
                std::fprintf(stderr, "net: the opponent named a fighter, blade or "
                                     "battleground this build does not have\n");
                session.stop();
                state = AppState::Menu;
            } else {
                for (int i = 0; i < 2; ++i) {
                    matchChars[i] = setup.chars[i];
                    matchWeapons[i] = setup.weapons[i];
                }
                matchLevel = setup.level;
                // Whichever fighter this machine owns is driven from here — by
                // the local player-1 controls, since a client has only its own
                // keyboard — and the other one comes off the wire.
                const int mine = session.localSlot();
                matchSources[mine] =
                    autoMatch ? InputSource::Bot : InputSource::Local0;
                matchSources[1 - mine] = InputSource::Remote;
                startMatch();
            }
        }

        // Both peers reached agreement on a rematch — independently, from the
        // same two flags — so each rolls its own session onto the next match
        // and rebuilds the same fresh Game. They need not do it on the same
        // frame: lockstep resynchronises at frame 0 of the new match the same
        // way it did at the start of the first one.
        if (state == AppState::Playing && session.active()) {
            const Session::Intent agreed = session.agreedIntent();
            if (agreed == Session::Intent::Rematch) {
                session.beginRematch();
                startMatch();
            } else if (agreed == Session::Intent::Reselect) {
                netReselect = true;
                // The loser names the next battleground. Both peers work that
                // out from the same winner(), so they agree without a word
                // about it, and it stops the host owning the ground forever.
                const int mine = session.localSlot();
                netOwnsLevel =
                    game->winner() >= 0 ? game->winner() != mine : mine == 0;
                if (autoMatch) {
                    // Nobody at this keyboard to pick again: keep the fighter
                    // and let the human on the other end change theirs.
                    session.submitLoadout(matchChars[mine], matchWeapons[mine],
                                          matchLevel, netOwnsLevel);
                    state = AppState::NetWait;
                } else {
                    // Back to the select screen with the connection intact —
                    // the new picks travel over the live session, so nobody
                    // re-hosts or re-joins to swap a sword.
                    beginSelect(mine == 0 ? SetupMode::NetHost : SetupMode::NetClient,
                                mine, netOwnsLevel);
                    state = AppState::CharacterSelect;
                }
            }
        }

        if (state == AppState::Playing) {
            // Local controls are read once per render frame — the attack edges
            // live on the latch until a step takes them.
            for (int i = 0; i < 2; ++i) {
                int slot = localSlot(matchSources[i]);
                if (slot >= 0) {
                    localInputs[slot].poll(window, localKeys(slot), frameTime);
                }
            }

            // One slot's input for one step. The bot is asked per step rather
            // than per frame because it reads sim state the last step moved.
            auto stepInput = [&](int i) -> PlayerInput {
                switch (matchSources[i]) {
                case InputSource::Local0:
                case InputSource::Local1:
                    return localInputs[localSlot(matchSources[i])].current();
                case InputSource::Bot:
                    // Once the match is decided the bot stands down; the human
                    // keeps control (walking off — or desecrating the corpse).
                    return game->over() ? PlayerInput{} : bot.think(*game, kFixedDt);
                case InputSource::Remote:
                    break; // no peer yet: the fighter stands still
                }
                return PlayerInput{};
            };

            accumulator += std::min(frameTime, 0.25f); // avoid spiral of death on stalls

            while (accumulator >= kFixedDt) {
                PlayerInput inputs[2];
                std::uint32_t expected = 0;
                bool checked = false;
                if (replayer.active()) {
                    // Replaying: the log is the only input source, and every
                    // step is checked against the state the recording had.
                    if (!replayer.next(inputs, expected)) {
                        if (!desynced) {
                            std::fprintf(stderr,
                                         "replay: %lld steps replayed, no desync\n",
                                         static_cast<long long>(replayer.frame()));
                        }
                        replayer.close();
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                        break;
                    }
                    checked = true;
                } else if (session.active()) {
                    // Lockstep: this machine's sample goes in, both fighters'
                    // inputs for this frame come out — or nothing does, and
                    // neither machine moves until the opponent's arrives.
                    if (!session.nextStep(stepInput(session.localSlot()), inputs)) {
                        break;
                    }
                } else {
                    inputs[0] = stepInput(0);
                    inputs[1] = stepInput(1);
                }

                game->update(inputs, kFixedDt);
                accumulator -= kFixedDt;
                for (int i = 0; i < 2; ++i) {
                    int slot = localSlot(matchSources[i]);
                    if (slot >= 0) {
                        localInputs[slot].consumeAttack();
                    }
                }

                // Only walked when the harness or a peer needs it; an offline
                // match never pays for the hash.
                if (checked || recorder.active() || session.active()) {
                    // A source emitting something the wire format can't carry
                    // would have the sim play one input and the log keep
                    // another — a desync with no cause anywhere near where it
                    // surfaces. Said once, at the step that does it.
                    for (int i = 0; i < 2 && !warnedLossy; ++i) {
                        if (!(unpackInput(packInput(inputs[i])) == inputs[i])) {
                            warnedLossy = true;
                            std::fprintf(stderr,
                                         "replay: player %d's input does not survive "
                                         "packInput — quantize it at the source "
                                         "(quantizeAxis, input.hpp)\n",
                                         i);
                        }
                    }
                    std::uint32_t sum = game->checksum();
                    if (checked && sum != expected && !desynced) {
                        // First mismatch only. Everything after it is
                        // downstream of the same divergence and would bury it.
                        desynced = true;
                        dumpDesync(*game, replayer.frame(), expected, sum);
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                    }
                    recorder.step(packInput(inputs[0]), packInput(inputs[1]), sum);
                    if (session.active()) {
                        session.stepped(sum); // the peer compares it against theirs
                    }
                }

                // Offline --auto stops on an exact step. Checking after the
                // loop instead would end the run wherever the last render
                // frame happened to land, so a recording's length — and its
                // hash — would wobble by a step between runs, which is a
                // regression check that cries wolf.
                if (autoMatch && !session.active() && game->over() &&
                    game->overTime() > 2.0f) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                    break;
                }
            }

            // A stall must not bank time. Without this the accumulator grows
            // for the whole wait and then sprints through the backlog the
            // instant the opponent's input lands: a few frames of catch-up is
            // recovery, a second of it is a teleport.
            if (session.active()) {
                accumulator = std::min(accumulator, kFixedDt * 8.0f);
            }

            // In a netplay session --auto asks for another round rather than
            // leaving: quitting would strand a peer who wanted a rematch, and
            // an endless run of matches is the more useful shape for the thing
            // --auto exists to do. (The offline case ends inside the step loop
            // above, where it can stop on an exact frame.)
            if (autoMatch && session.active() && game->over() &&
                game->overTime() > 2.0f) {
                // A bot peer goes along with whatever the human asked for, so
                // one driven window is enough to exercise either agreement.
                const Session::Intent theirs = session.peerRequested();
                if (theirs != Session::Intent::None) {
                    session.request(theirs);
                } else if (session.requested() == Session::Intent::None) {
                    session.request(Session::Intent::Rematch);
                }
            }
        } else {
            accumulator = 0.0f; // the sim freezes while the menu is up
        }

        // Music follows the app state: the front-end (menu + select screens)
        // plays the menu theme, a match plays its battleground's own track.
        // playMusic is an idempotent "this should be the music now", so the
        // crossfades fall out of the state transitions on their own.
        audio.playMusic(state == AppState::Playing ? levelMusic(game->level())
                                                   : Music::Menu);

        // Play the sounds the sim raised this frame, panned by world x
        // relative to the fighters' midpoint (~ the camera's framing center),
        // with a little pitch jitter so repeats don't sound stamped-out.
        if (!game->soundCues().empty()) {
            float midX = 0.5f * (game->player(0).pos.x + game->player(1).pos.x);
            for (const SoundCue& cue : game->soundCues()) {
                sfxSeed = sfxSeed * 1664525u + 1013904223u;
                float jitter = static_cast<float>(sfxSeed >> 8) * (1.0f / 16777216.0f);
                float pan = std::clamp((cue.x - midX) * 0.08f, -0.6f, 0.6f);
                audio.play(cue.sfx, pan, 0.94f + 0.12f * jitter, cue.gain);
            }
            game->clearSoundCues();
        }

        camera.update(game->player(0).pos, game->player(1).pos, renderer.aspect(),
                      frameTime);

        MenuAction action = MenuAction::None;
        OverAction overAction = OverAction::None;
        OptionsResult optionsResult;
        NetSetupResult netResult;
        SelectResult picked;
        if (renderer.beginFrame()) {
            renderer.setViewProj(camera.proj(renderer.aspect()) * camera.view());
            drawScene(renderer, *game, elapsed);
            if (state == AppState::Menu) {
                action = drawMainMenu();
            } else if (state == AppState::Options) {
                optionsResult = drawOptions(window, settings, options);
            } else if (state == AppState::NetSetup) {
                netResult = drawNetSetup(netScreen, steamMode, steamTransport.selfId());
            } else if (state == AppState::CharacterSelect) {
                picked = drawCharacterSelect(select);
            } else if (state == AppState::NetWait) {
                drawNetBanner(session); // the only thing on screen before a match
            } else {
                drawBloodBars(*game);
                if (session.active()) {
                    drawNetStatus(session);
                    if (session.waiting()) {
                        drawNetBanner(session);
                    }
                }
                // Give the killing blow a beat to land before the overlay.
                // Not while replaying: Rematch would rebuild the Game out from
                // under the log and report a desync that is purely the click's.
                if (!replayer.active() && game->over() && game->overTime() > 1.2f) {
                    overAction = drawWinOverlay(
                        *game, matchSources[1] == InputSource::Local1,
                        session.active() ? session.localSlot() : 0, session.active(),
                        session.requested() != Session::Intent::None);
                }
            }
            renderer.endFrame();
        }

        if (action == MenuAction::Play) {
            beginSelect(SetupMode::Solo, 0, true);
            state = AppState::CharacterSelect;
        } else if (action == MenuAction::Versus) {
            beginSelect(SetupMode::LocalVersus, 0, true);
            state = AppState::CharacterSelect;
        } else if (action == MenuAction::Online) {
            netScreen.error[0] = '\0';
            state = AppState::NetSetup;
        } else if (action == MenuAction::Options) {
            state = AppState::Options;
            options = OptionsScreen{};
        } else if (action == MenuAction::Quit) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Persist on every settled edit: there's no apply step, so the file and
        // what the game is reading never disagree. Keybinds are read live out of
        // `settings`; the mixer levels have to be pushed across to Audio.
        if (optionsResult.applyAudio) {
            audio.setMusicVolume(settings.audio.music);
            audio.setSfxVolume(settings.audio.sfx);
        }
        if (optionsResult.previewSfx) {
            audio.play(Sfx::Hit, 0.0f, 1.0f);
        }
        if (optionsResult.save) {
            saveConfig(settings);
        }
        if (optionsResult.back) {
            state = AppState::Menu;
        }

        // Online setup. Opening the socket is the thing that can fail, and it
        // fails here where the player can still fix the field, rather than
        // after they have gone and picked a fighter.
        if (netResult.back) {
            state = AppState::Menu;
        } else if (netResult.host || netResult.join) {
            netScreen.error[0] = '\0';
            bool ready = false;
            if (steamMode) {
                // Over the relay there is nothing to bind and nothing to
                // resolve: hosting is just agreeing to answer, and joining is
                // naming a person.
                if (netResult.host) {
                    ready = steamTransport.listen();
                } else {
                    char* end = nullptr;
                    unsigned long long id =
                        std::strtoull(netScreen.steamId, &end, 10);
                    if (!end || *end != '\0' || id == 0ull) {
                        std::snprintf(netScreen.error, sizeof netScreen.error,
                                      "\"%s\" is not a SteamID - it is 17 digits.",
                                      netScreen.steamId);
                    } else {
                        ready = steamTransport.connectTo(id);
                    }
                }
                if (!ready && netScreen.error[0] == '\0') {
                    std::snprintf(netScreen.error, sizeof netScreen.error,
                                  "Steam is not ready.");
                }
            } else if (netResult.host) {
                long port = std::strtol(netScreen.port, nullptr, 10);
                if (port <= 0 || port > 65535) {
                    std::snprintf(netScreen.error, sizeof netScreen.error,
                                  "\"%s\" is not a port number.", netScreen.port);
                } else if (!transport.host(static_cast<std::uint16_t>(port))) {
                    std::snprintf(netScreen.error, sizeof netScreen.error,
                                  "Could not listen on port %ld - already in use?",
                                  port);
                } else {
                    ready = true;
                }
            } else {
                std::string h;
                std::uint16_t p = 0;
                if (!parseEndpoint(netScreen.address, h, p)) {
                    std::snprintf(netScreen.error, sizeof netScreen.error,
                                  "\"%s\" should look like 192.168.1.20:7777.",
                                  netScreen.address);
                } else if (!transport.join(h.c_str(), p)) {
                    std::snprintf(netScreen.error, sizeof netScreen.error,
                                  "Could not resolve \"%s\".", h.c_str());
                } else {
                    ready = true;
                }
            }
            if (ready) {
                // The host picks a fighter and the ground; the client picks a
                // fighter into the ground the host is about to name.
                const bool hosting = netResult.host;
                beginSelect(hosting ? SetupMode::NetHost : SetupMode::NetClient,
                            hosting ? 0 : 1, hosting);
                state = AppState::CharacterSelect;
            }
        }

        if (picked.character >= 0) {
            matchChars[selectingPlayer] = picked.character;
            matchWeapons[selectingPlayer] = picked.weapon;
            if (picked.level >= 0) {
                matchLevel = picked.level; // -1 when the ground was already set
            }
            if (setupMode == SetupMode::LocalVersus && selectingPlayer == 0) {
                beginSelect(SetupMode::LocalVersus, 1, false); // hand the keyboard over
            } else if (setupMode == SetupMode::Solo) {
                // One human, one bot: the opponent is drawn rather than picked.
                matchChars[1] =
                    std::uniform_int_distribution<int>{0, kCharacterCount - 1}(rng);
                matchWeapons[1] =
                    std::uniform_int_distribution<int>{0, kWeaponCount - 1}(rng);
                matchSources[0] = InputSource::Local0;
                matchSources[1] = InputSource::Bot;
                startMatch();
            } else if (setupMode == SetupMode::LocalVersus) {
                matchSources[0] = InputSource::Local0;
                matchSources[1] = InputSource::Local1;
                startMatch();
            } else if (netReselect) {
                // Re-picking mid-session: the connection is already up, so the
                // pick goes straight down it rather than through a handshake.
                session.submitLoadout(matchChars[selectingPlayer],
                                      matchWeapons[selectingPlayer], matchLevel,
                                      netOwnsLevel);
                state = AppState::NetWait;
            } else {
                // Netplay: this half of the match goes to the session, and the
                // arena waits on the handshake for the other half.
                beginNetSession(setupMode == SetupMode::NetHost ? Session::Role::Host
                                                                : Session::Role::Client);
                state = AppState::NetWait;
            }
        }

        if (overAction == OverAction::Rematch) {
            // Offline it happens now; across a wire it is a request, and the
            // restart lands for both peers when the other agrees.
            if (session.active()) {
                session.request(Session::Intent::Rematch);
            } else {
                startMatch();
            }
        } else if (overAction == OverAction::Select) {
            if (session.active()) {
                session.request(Session::Intent::Reselect); // also has to be agreed
            } else {
                beginSelect(setupMode, 0, true);
                state = AppState::CharacterSelect;
            }
        } else if (overAction == OverAction::Disconnect) {
            session.stop();
            state = AppState::Menu;
        }
    }

    session.stop(); // reports the frame and stall counts on the way out
    if (simLink && simLink->dropped() > 0) {
        std::fprintf(stderr, "net: the simulated link dropped %lld datagrams\n",
                     static_cast<long long>(simLink->dropped()));
    }
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
