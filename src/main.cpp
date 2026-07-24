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

#include "audio.hpp"
#include "camera.hpp"
#include "character.hpp"
#include "game.hpp"
#include "renderer.hpp"
#include "samurai.hpp"

namespace {

enum class AppState { Menu, CharacterSelect, Playing };
enum class MenuAction { None, Play, Quit };

// Character-select screen state. Both players browse and lock in
// simultaneously, MK-style; a match starts once both are locked.
struct SelectScreen {
    int sel[2] = {0, 1};
    bool locked[2] = {false, false};
};

// One frame's worth of select-screen key edges, per player.
struct SelectNav {
    bool left[2] = {};
    bool right[2] = {};
    bool confirm[2] = {};
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

// MK-style select: a row of tiles both players highlight at once. P1 keeps a
// crimson frame (inset), P2 an indigo one (outset), so a mirror pick shows
// both. Mouse clicking a tile picks-and-locks for P1. Returns true when both
// players have locked in and the match should start.
bool drawCharacterSelect(SelectScreen& s, const SelectNav& nav) {
    for (int p = 0; p < 2; ++p) {
        if (s.locked[p]) {
            continue;
        }
        if (nav.left[p]) {
            s.sel[p] = (s.sel[p] + kCharacterCount - 1) % kCharacterCount;
        }
        if (nav.right[p]) {
            s.sel[p] = (s.sel[p] + 1) % kCharacterCount;
        }
        if (nav.confirm[p]) {
            s.locked[p] = true;
        }
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            {0.5f, 0.5f});
    ImGui::Begin("char-select", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(nullptr, 50.0f);
    ImGui::TextUnformatted("CHOOSE YOUR FIGHTER");
    ImGui::PopFont();
    ImGui::Dummy({0.0f, 6.0f});

    const float tile = 132.0f;
    const float tileGap = 14.0f;
    for (int i = 0; i < kCharacterCount; ++i) {
        const CharacterDef& c = characterDef(i);
        if (i > 0) {
            ImGui::SameLine(0.0f, tileGap);
        }
        ImGui::PushID(i);
        // Tile face: the character's kimono color, brightening on hover.
        ImVec4 base{c.colors.kimono.r, c.colors.kimono.g, c.colors.kimono.b, 0.75f};
        ImVec4 hot{base.x * 1.25f + 0.05f, base.y * 1.25f + 0.05f,
                   base.z * 1.25f + 0.05f, 0.9f};
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hot);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, hot);
        // Dark name text on bright tiles (Kensei's undyed kimono), cream on
        // the rest.
        float lum = 0.2126f * base.x + 0.7152f * base.y + 0.0722f * base.z;
        ImGui::PushStyleColor(ImGuiCol_Text, lum > 0.45f
                                                 ? ImVec4{0.15f, 0.13f, 0.11f, 1.0f}
                                                 : ImVec4{0.91f, 0.87f, 0.80f, 1.0f});
        ImGui::PushFont(nullptr, 26.0f);
        if (ImGui::Button(c.name, {tile, tile}) && !s.locked[0]) {
            s.sel[0] = i;
            s.locked[0] = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopID();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        if (s.sel[0] == i) {
            dl->AddRect({mn.x + 3.0f, mn.y + 3.0f}, {mx.x - 3.0f, mx.y - 3.0f},
                        IM_COL32(214, 44, 44, 255), 2.0f, 0, s.locked[0] ? 5.0f : 2.5f);
        }
        if (s.sel[1] == i) {
            dl->AddRect({mn.x - 2.0f, mn.y - 2.0f}, {mx.x + 2.0f, mx.y + 2.0f},
                        IM_COL32(64, 96, 224, 255), 2.0f, 0, s.locked[1] ? 5.0f : 2.5f);
        }
    }
    ImGui::Dummy({0.0f, 10.0f});

    // Per-player detail panels: name, epithet, stat bars, ready/hint line.
    // The menu style's roomy 16px item spacing would push the lower rows out
    // of the fixed-height panels; tighten it locally.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 6.0f});
    const float panelW = (kCharacterCount * tile + (kCharacterCount - 1) * tileGap -
                          tileGap) * 0.5f;
    const ImU32 pFill[2] = {IM_COL32(214, 44, 44, 255), IM_COL32(64, 96, 224, 255)};
    const char* pTag[2] = {"P1", "P2"};
    const char* pHint[2] = {"A / D pick    Space lock", "< / > pick    R.Shift lock"};
    for (int p = 0; p < 2; ++p) {
        if (p > 0) {
            ImGui::SameLine(0.0f, tileGap);
        }
        ImGui::BeginChild(pTag[p], {panelW, 200.0f}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        const CharacterDef& c = characterDef(s.sel[p]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(pFill[p]));
        ImGui::PushFont(nullptr, 22.0f);
        ImGui::TextUnformatted(pTag[p]);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::SameLine(46.0f);
        ImGui::PushFont(nullptr, 30.0f);
        ImGui::TextUnformatted(c.name);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.65f});
        ImGui::PushFont(nullptr, 17.0f);
        ImGui::TextUnformatted(c.epithet);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0f, 2.0f});
        drawStatBar("Speed", c.rSpeed, pFill[p]);
        drawStatBar("Power", c.rPower, pFill[p]);
        drawStatBar("Reach", c.rReach, pFill[p]);
        drawStatBar("Weight", c.rWeight, pFill[p]);
        ImGui::Dummy({0.0f, 2.0f});
        ImGui::PushFont(nullptr, 17.0f);
        if (s.locked[p]) {
            ImGui::PushStyleColor(ImGuiCol_Text, {0.85f, 0.70f, 0.25f, 1.0f});
            ImGui::TextUnformatted("READY");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, {0.91f, 0.87f, 0.80f, 0.45f});
            ImGui::TextUnformatted(pHint[p]);
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
        ImGui::EndChild();
    }
    ImGui::PopStyleVar();

    ImGui::End();
    return s.locked[0] && s.locked[1];
}

void drawBox(Renderer& renderer, glm::vec3 center, glm::vec3 size, glm::vec4 color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);
    renderer.drawBox(model, color);
}

PlayerInput readInput(GLFWwindow* window, int left, int right, int away, int toward,
                      int jump) {
    PlayerInput in;
    if (glfwGetKey(window, left) == GLFW_PRESS) in.move.x -= 1.0f;
    if (glfwGetKey(window, right) == GLFW_PRESS) in.move.x += 1.0f;
    if (glfwGetKey(window, away) == GLFW_PRESS) in.move.y -= 1.0f;   // into the screen (-z)
    if (glfwGetKey(window, toward) == GLFW_PRESS) in.move.y += 1.0f; // toward the camera (+z)
    in.jump = glfwGetKey(window, jump) == GLFW_PRESS;
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

    // Fighters, in their chosen character's colors and blade length.
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        const CharacterDef& def = game.character(i);
        float yaw = p.facing > 0.0f ? 0.0f : 3.14159265358979f;
        glm::vec3 feet{p.pos.x, p.pos.y - Player::kHalfHeight, p.pos.z};
        SamuraiColors c = def.colors;
        if (p.hitstun > 0.0f) {
            c.kimono = glm::mix(c.kimono, glm::vec4(1.0f), 0.7f * p.hitstun / 0.35f);
        }
        drawSamurai(renderer, feet, yaw,
                    {p.animPhase, p.moveAmount, p.grounded, time,
                     static_cast<int>(p.attackState), p.attackT, def.stats.reach,
                     p.bodyRoll(), p.severed},
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
    auto game = std::make_unique<Game>(0, 1);
    FramingCamera camera;
    std::uint32_t sfxSeed = 0xb0051d0u; // pitch-jitter rng
    AppState state = AppState::Menu;
    SelectScreen select;

    // Select-screen navigation keys, edge-detected: {P1 left,right,confirm,
    // P2 left,right,confirm}. Held state is re-seeded when the screen opens
    // so the press that opened it can't navigate or lock.
    const int kNavKeys[6] = {GLFW_KEY_A,    GLFW_KEY_D,     GLFW_KEY_SPACE,
                             GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_RIGHT_SHIFT};
    bool navHeld[6] = {};

    constexpr float kFixedDt = 1.0f / 120.0f;
    float accumulator = 0.0f;
    float elapsed = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    // Attack presses are edge-detected per render frame but consumed by fixed
    // steps; latch them so a click landing on a zero-step frame is not lost.
    bool attackHeld[2] = {false, false};
    bool attackPending[2] = {false, false};
    bool escHeld = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Esc backs out one level: match/select -> menu, menu -> quit.
        bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !escHeld) {
            if (state != AppState::Menu) {
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
                          GLFW_KEY_SPACE),
                readInput(window, GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
                          GLFW_KEY_RIGHT_CONTROL),
            };

            bool held[2] = {
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
                glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS,
            };
            for (int i = 0; i < 2; ++i) {
                if (held[i] && !attackHeld[i]) {
                    attackPending[i] = true;
                }
                attackHeld[i] = held[i];
                inputs[i].attack = attackPending[i];
            }

            accumulator += std::min(frameTime, 0.25f); // avoid spiral of death on stalls

            while (accumulator >= kFixedDt) {
                game->update(inputs, kFixedDt);
                accumulator -= kFixedDt;
                attackPending[0] = attackPending[1] = false;
                inputs[0].attack = inputs[1].attack = false;
            }
        } else {
            accumulator = 0.0f; // the sim freezes while the menu is up
        }

        // Select-screen navigation edges (empty in every other state).
        SelectNav nav;
        if (state == AppState::CharacterSelect) {
            for (int k = 0; k < 6; ++k) {
                bool down = glfwGetKey(window, kNavKeys[k]) == GLFW_PRESS;
                bool edge = down && !navHeld[k];
                navHeld[k] = down;
                int p = k / 3;
                if (k % 3 == 0) nav.left[p] = edge;
                if (k % 3 == 1) nav.right[p] = edge;
                if (k % 3 == 2) nav.confirm[p] = edge;
            }
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
        bool startMatch = false;
        if (renderer.beginFrame()) {
            renderer.setViewProj(camera.proj(renderer.aspect()) * camera.view());
            drawScene(renderer, *game, elapsed);
            if (state == AppState::Menu) {
                action = drawMainMenu();
            } else if (state == AppState::CharacterSelect) {
                startMatch = drawCharacterSelect(select, nav);
            }
            renderer.endFrame();
        }

        if (action == MenuAction::Play) {
            state = AppState::CharacterSelect;
            select = SelectScreen{};
            // Seed the nav latches from the live key state so whatever press
            // activated Play (Space, arrows) doesn't navigate or lock.
            for (int k = 0; k < 6; ++k) {
                navHeld[k] = glfwGetKey(window, kNavKeys[k]) == GLFW_PRESS;
            }
        } else if (action == MenuAction::Quit) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        if (startMatch) {
            game = std::make_unique<Game>(select.sel[0], select.sel[1]);
            state = AppState::Playing;
            // Seed the attack latches from the live button state so the click
            // that locked a character doesn't read as a swing next frame.
            attackHeld[0] = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            attackHeld[1] = glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            attackPending[0] = attackPending[1] = false;
        }
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
