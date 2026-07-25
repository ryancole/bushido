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
#include <memory>
#include <random>

#include "audio.hpp"
#include "bot.hpp"
#include "camera.hpp"
#include "character.hpp"
#include "game.hpp"
#include "renderer.hpp"
#include "samurai.hpp"
#include "weapon.hpp"

namespace {

enum class AppState { Menu, CharacterSelect, Playing };
enum class MenuAction { None, Play, Quit };

// Select-screen state. Two stages on one screen: pick the fighter, then the
// blade. `shown`/`shownWeapon` are the indices being previewed (the last tile
// the mouse hovered); pickedCharacter latches once a fighter tile is clicked
// and flips the screen to the weapon row.
struct SelectScreen {
    int shown = 0;
    int shownWeapon = 0;
    int pickedCharacter = -1;
};

// A completed loadout pick, returned by drawCharacterSelect once the weapon
// (the second stage) is clicked; character stays -1 until then.
struct SelectResult {
    int character = -1;
    int weapon = -1;
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
    if (ImGui::Button("Quit", {buttonWidth, 0.0f})) {
        action = MenuAction::Quit;
    }
    ImGui::PopFont();

    ImGui::End();
    return action;
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

// Single-player select, two stages on one screen, both mouse-driven: a row of
// fighter tiles, then (once one is clicked) a row of weapon tiles. Hovering a
// tile previews its stats in the panel below; clicking a weapon locks the
// loadout and starts the match (the caller draws the opponent — fighter and
// blade — at random). Returns character -1 while still browsing.
SelectResult drawCharacterSelect(SelectScreen& s) {
    SelectResult picked;
    const bool weaponStage = s.pickedCharacter >= 0;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("char-select", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 50.0f);
    ImGui::TextUnformatted(weaponStage ? "CHOOSE YOUR BLADE" : "CHOOSE YOUR FIGHTER");
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 6.0f});

    const float tile = 132.0f;
    const float tileGap = 14.0f;
    // The panel keeps the fighter row's width in both stages so the window
    // (and the title above it) doesn't jump when the rows swap.
    const float panelW = kCharacterCount * tile + (kCharacterCount - 1) * tileGap;

    if (!weaponStage) {
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
    } else {
        for (int i = 0; i < kWeaponCount; ++i) {
            const WeaponDef& w = weaponDef(i);
            if (i > 0) {
                ImGui::SameLine(0.0f, tileGap);
            }
            ImGui::PushID(i);
            if (drawSelectTile(w.name, w.tileColor, tile, s.shownWeapon == i)) {
                picked = {s.pickedCharacter, i};
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                s.shownWeapon = i;
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
    if (weaponStage) {
        const WeaponDef& w = weaponDef(s.shownWeapon);
        name = w.name;
        epithet = w.epithet;
    } else {
        const CharacterDef& c = characterDef(s.shown);
        name = c.name;
        epithet = c.epithet;
    }
    ImGui::PushFont(nullptr, 30.0f);
    if (weaponStage) {
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
    if (weaponStage) {
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
    ImGui::TextUnformatted(weaponStage
                               ? "Click a blade to begin - your opponent is drawn at random"
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

PlayerInput readInput(GLFWwindow* window, int left, int right, int away, int toward,
                      int jump, int block, int crouch) {
    PlayerInput in;
    if (glfwGetKey(window, left) == GLFW_PRESS) in.move.x -= 1.0f;
    if (glfwGetKey(window, right) == GLFW_PRESS) in.move.x += 1.0f;
    if (glfwGetKey(window, away) == GLFW_PRESS) in.move.y -= 1.0f;   // into the screen (-z)
    if (glfwGetKey(window, toward) == GLFW_PRESS) in.move.y += 1.0f; // toward the camera (+z)
    in.jump = glfwGetKey(window, jump) == GLFW_PRESS;
    in.block = glfwGetKey(window, block) == GLFW_PRESS;
    in.crouch = glfwGetKey(window, crouch) == GLFW_PRESS;
    return in;
}

void drawScene(Renderer& renderer, const Game& game, float time) {
    // Arena floor: covers the playable x/z area with some visual overhang.
    drawBox(renderer, {0.0f, -0.5f, 0.0f},
            {Game::kArenaHalfWidth * 2.0f + 8.0f, 1.0f, Game::kArenaHalfDepth * 2.0f + 4.0f},
            {0.16f, 0.15f, 0.13f, 1.0f});

    // Background pillars for depth reference, behind the playable area.
    const float pillarX[] = {-14.0f, -7.0f, 0.0f, 7.0f, 14.0f};
    const float pillarH[] = {4.5f, 3.2f, 5.5f, 3.8f, 4.8f};
    for (int i = 0; i < 5; ++i) {
        drawBox(renderer, {pillarX[i], pillarH[i] * 0.5f, -Game::kArenaHalfDepth - 1.5f},
                {1.2f, pillarH[i], 1.2f}, {0.10f, 0.10f, 0.14f, 1.0f});
    }

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
    // The initial Game is just the arena diorama behind the menu; every
    // lock-in on the select screen replaces it with a fresh match.
    auto game = std::make_unique<Game>(0, 0, 1, 0);
    Bot bot; // drives player 2; re-seeded per match
    FramingCamera camera;
    std::uint32_t sfxSeed = 0xb0051d0u; // pitch-jitter rng
    AppState state = AppState::Menu;
    SelectScreen select;
    std::mt19937 rng{std::random_device{}()}; // opponent draw on the select screen
    // The current match's loadout — (character, weapon) roster indices per
    // player — kept outside Game so Rematch can rebuild the same pairing.
    int matchChars[2] = {0, 1};
    int matchWeapons[2] = {0, 0};

    constexpr float kFixedDt = 1.0f / 120.0f;
    float accumulator = 0.0f;
    float elapsed = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    // Attack inputs are edge-detected per render frame but consumed by fixed
    // steps; latch them so a click landing on a zero-step frame is not lost.
    // The left button attacks on *release* — a tap is the normal swing, a
    // hold past kHeavyHoldTime charges the heavy — so both can share the
    // button. The right button jabs on press.
    constexpr float kHeavyHoldTime = 0.28f; // s of LMB hold that makes it a heavy
    bool lmbHeld = false;
    bool rmbHeld = false;
    bool lmbSuppressed = false; // ignore the release of a pre-match click
    float lmbHoldTime = 0.0f;
    bool attackPending = false;
    AttackKind pendingKind = AttackKind::Light;
    bool escHeld = false;

    // Fresh match from matchChars. Seeding the button latches from the live
    // state keeps the click that started the match from reading as an attack:
    // a held LMB is flagged so even its eventual release swings nothing.
    auto startMatch = [&] {
        game = std::make_unique<Game>(matchChars[0], matchWeapons[0], matchChars[1],
                                      matchWeapons[1]);
        bot = Bot{1, rng()}; // fresh brain (and rng stream) each match
        state = AppState::Playing;
        lmbHeld = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        rmbHeld = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        lmbSuppressed = lmbHeld;
        lmbHoldTime = 0.0f;
        attackPending = false;
    };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Esc backs out one level: blade stage -> fighter stage,
        // match/select -> menu, menu -> quit.
        bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !escHeld) {
            if (state == AppState::CharacterSelect && select.pickedCharacter >= 0) {
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

        if (state == AppState::Playing) {
            PlayerInput inputs[2] = {
                readInput(window, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_W, GLFW_KEY_S,
                          GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_CONTROL),
                {}, // player 2 is the bot, filled per fixed step below
            };

            // LMB: light on a tap's release, heavy on a long hold's release.
            bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (lmb && !lmbHeld) {
                lmbHoldTime = 0.0f; // fresh press: start the charge clock
            }
            if (lmb) {
                lmbHoldTime += frameTime;
            } else if (lmbHeld) { // release edge
                if (lmbSuppressed) {
                    lmbSuppressed = false; // the click that started the match
                } else if (!attackPending) {
                    pendingKind = lmbHoldTime >= kHeavyHoldTime ? AttackKind::Heavy
                                                                : AttackKind::Light;
                    attackPending = true;
                }
            }
            lmbHeld = lmb;

            // RMB: jab on press.
            bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            if (rmb && !rmbHeld && !attackPending) {
                pendingKind = AttackKind::Jab;
                attackPending = true;
            }
            rmbHeld = rmb;

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
        SelectResult picked;
        if (renderer.beginFrame()) {
            renderer.setViewProj(camera.proj(renderer.aspect()) * camera.view());
            drawScene(renderer, *game, elapsed);
            if (state == AppState::Menu) {
                action = drawMainMenu();
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
        } else if (action == MenuAction::Quit) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        if (picked.character >= 0) {
            matchChars[0] = picked.character;
            matchWeapons[0] = picked.weapon;
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
