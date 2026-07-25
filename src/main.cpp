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
#include "levels/level.hpp"
#include "renderer.hpp"
#include "samurai.hpp"
#include "weapons/weapon.hpp"

namespace {

enum class AppState { Loading, Menu, Options, CharacterSelect, Playing };
enum class MenuAction { None, Play, Options, Quit };

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

// Options sections, in tab order. One per `Settings` member — a new group of
// settings is a struct there plus a tab here.
enum class OptionsSection { Keybinds, Audio, Count };
constexpr int kOptionsSectionCount = static_cast<int>(OptionsSection::Count);
constexpr const char* kSectionNames[kOptionsSectionCount] = {"Keybinds", "Audio"};

// Options-screen state. `capturing` is the keybind row waiting for a control;
// the two latches keep the clicks that open and close a capture from being read
// as the binding itself (see drawOptions).
struct OptionsScreen {
    OptionsSection section = OptionsSection::Keybinds;
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
            const Action a = static_cast<Action>(s.capturing);
            const Bind previous = settings.keybinds[a];
            // A control that's already spoken for trades places with this row
            // instead of being refused: every action stays bound, so there's
            // no unbound state to render, poll, or save.
            for (Bind& b : settings.keybinds.binds) {
                if (b == pressed) {
                    b = previous;
                }
            }
            settings.keybinds[a] = pressed;
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
    if (s.section == OptionsSection::Keybinds) {
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
                capturing ? "press a control" : bindName(settings.keybinds[a]),
                {fieldW, 0.0f});
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
                           s.section == OptionsSection::Keybinds
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
        if (s.section == OptionsSection::Keybinds) {
            settings.keybinds = defaultKeybinds();
            s.capturing = -1;
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
    const bool levelStage = s.pickedWeapon >= 0;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("char-select", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 50.0f);
    ImGui::TextUnformatted(levelStage    ? "CHOOSE YOUR BATTLEGROUND"
                           : weaponStage ? "CHOOSE YOUR BLADE"
                                         : "CHOOSE YOUR FIGHTER");
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
    ImGui::TextUnformatted(
        levelStage    ? "Click a battleground to begin - your opponent is drawn at random"
        : weaponStage ? "Click a blade to choose the battleground"
                      : "Click a fighter to choose their blade");
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
enum class OverAction { None, Rematch, Select };

OverAction drawWinOverlay(const Game& game) {
    OverAction action = OverAction::None;
    const char* title = game.winner() == 0 ? "VICTORY" : "SLAIN";

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
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Rematch", {buttonWidth, 0.0f})) {
        action = OverAction::Rematch;
    }
    ImGui::SetCursorPosX(buttonX);
    if (ImGui::Button("Character Select", {buttonWidth, 0.0f})) {
        action = OverAction::Select;
    }
    ImGui::PopFont();

    ImGui::End();
    return action;
}

void drawBox(Renderer& renderer, glm::vec3 center, glm::vec3 size, glm::vec4 color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);
    renderer.drawBox(model, color);
}

// The held-state half of the player's controls; the attack edges are latched in
// the main loop instead (they have to survive zero-step frames).
PlayerInput readInput(GLFWwindow* window, const Keybinds& keys) {
    PlayerInput in;
    if (bindHeld(window, keys[Action::MoveLeft])) in.move.x -= 1.0f;
    if (bindHeld(window, keys[Action::MoveRight])) in.move.x += 1.0f;
    if (bindHeld(window, keys[Action::MoveAway])) in.move.y -= 1.0f;   // into the screen (-z)
    if (bindHeld(window, keys[Action::MoveToward])) in.move.y += 1.0f; // toward the camera (+z)
    in.jump = bindHeld(window, keys[Action::Jump]);
    in.block = bindHeld(window, keys[Action::Block]);
    in.crouch = bindHeld(window, keys[Action::Crouch]);
    return in;
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

int main() {
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
    std::mt19937 rng{std::random_device{}()}; // opponent draw on the select screen
    // The current match's loadout — (character, weapon) roster indices per
    // player — kept outside Game so Rematch can rebuild the same pairing.
    int matchChars[2] = {0, 1};
    int matchWeapons[2] = {0, 0};
    // The battleground index rides beside the loadout so Rematch keeps the
    // arena; Game bakes it in at construction (scenery + obstacle colliders).
    int matchLevel = 0;

    constexpr float kFixedDt = 1.0f / 120.0f;
    float accumulator = 0.0f;
    float elapsed = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    // Attack inputs are edge-detected per render frame but consumed by fixed
    // steps; latch them so a press landing on a zero-step frame is not lost.
    // The attack control fires on *release* — a tap is the normal swing, a
    // hold past kHeavyHoldTime charges the heavy — so both share one control.
    // The jab control fires on press.
    constexpr float kHeavyHoldTime = 0.28f; // s of hold that makes it a heavy
    bool attackHeld = false;
    bool jabHeld = false;
    bool attackSuppressed = false; // ignore the release of a pre-match click
    float attackHoldTime = 0.0f;
    bool attackPending = false;
    AttackKind pendingKind = AttackKind::Light;
    bool escHeld = false;

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
        bot = Bot{1, rng()}; // fresh brain (and rng stream) each match
        state = AppState::Playing;
        attackHeld = bindHeld(window, settings.keybinds[Action::Attack]);
        jabHeld = bindHeld(window, settings.keybinds[Action::Jab]);
        attackSuppressed = attackHeld;
        attackHoldTime = 0.0f;
        attackPending = false;
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
                state = AppState::Menu;
            }
            continue; // nothing below this exists yet
        }

        if (state == AppState::Playing) {
            PlayerInput inputs[2] = {
                readInput(window, settings.keybinds),
                {}, // player 2 is the bot, filled per fixed step below
            };

            // Attack: light on a tap's release, heavy on a long hold's release.
            bool attack = bindHeld(window, settings.keybinds[Action::Attack]);
            if (attack && !attackHeld) {
                attackHoldTime = 0.0f; // fresh press: start the charge clock
            }
            if (attack) {
                attackHoldTime += frameTime;
            } else if (attackHeld) { // release edge
                if (attackSuppressed) {
                    attackSuppressed = false; // the click that started the match
                } else if (!attackPending) {
                    pendingKind = attackHoldTime >= kHeavyHoldTime ? AttackKind::Heavy
                                                                  : AttackKind::Light;
                    attackPending = true;
                }
            }
            attackHeld = attack;

            // Jab on press.
            bool jab = bindHeld(window, settings.keybinds[Action::Jab]);
            if (jab && !jabHeld && !attackPending) {
                pendingKind = AttackKind::Jab;
                attackPending = true;
            }
            jabHeld = jab;

            inputs[0].attack = attackPending;
            inputs[0].attackKind = pendingKind;

            accumulator += std::min(frameTime, 0.25f); // avoid spiral of death on stalls

            while (accumulator >= kFixedDt) {
                // Once the match is decided the bot stands down; the human
                // keeps control (walking off — or desecrating the corpse).
                inputs[1] = game->over() ? PlayerInput{} : bot.think(*game, kFixedDt);
                game->update(inputs, kFixedDt);
                accumulator -= kFixedDt;
                attackPending = false;
                inputs[0].attack = false;
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
        SelectResult picked;
        if (renderer.beginFrame()) {
            renderer.setViewProj(camera.proj(renderer.aspect()) * camera.view());
            drawScene(renderer, *game, elapsed);
            if (state == AppState::Menu) {
                action = drawMainMenu();
            } else if (state == AppState::Options) {
                optionsResult = drawOptions(window, settings, options);
            } else if (state == AppState::CharacterSelect) {
                picked = drawCharacterSelect(select);
            } else {
                drawBloodBars(*game);
                // Give the killing blow a beat to land before the overlay.
                if (game->over() && game->overTime() > 1.2f) {
                    overAction = drawWinOverlay(*game);
                }
            }
            renderer.endFrame();
        }

        if (action == MenuAction::Play) {
            state = AppState::CharacterSelect;
            select = SelectScreen{};
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

        if (picked.character >= 0) {
            matchChars[0] = picked.character;
            matchWeapons[0] = picked.weapon;
            matchLevel = picked.level;
            matchChars[1] =
                std::uniform_int_distribution<int>{0, kCharacterCount - 1}(rng);
            matchWeapons[1] =
                std::uniform_int_distribution<int>{0, kWeaponCount - 1}(rng);
            startMatch();
        }

        if (overAction == OverAction::Rematch) {
            startMatch();
        } else if (overAction == OverAction::Select) {
            state = AppState::CharacterSelect;
            select = SelectScreen{};
        }
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
