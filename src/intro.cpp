#include "intro.hpp"

#include "characters/character.hpp"
#include "weapons/weapon.hpp"

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {

// The front end's palette (setupMenuStyle, drawLoadingScreen, the blood bars):
// cream on near-black with a crimson edge, and the riposte gold for the one
// thing on screen that is shouting.
constexpr ImU32 kCream = IM_COL32(232, 222, 204, 255);
constexpr ImU32 kCrimson = IM_COL32(214, 44, 44, 255);
constexpr ImU32 kGold = IM_COL32(217, 179, 64, 255);
constexpr ImU32 kInk = IM_COL32(8, 6, 6, 255);

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

// 0..1 eased to 0..1, flat at both ends.
float smooth(float u) {
    u = clamp01(u);
    return u * u * (3.0f - 2.0f * u);
}

// Slams to the mark and settles back — the overshoot is the whole reason a
// plate sliding in reads as a slab landing rather than a panel animating.
float overshoot(float u) {
    u = clamp01(u);
    const float c = 1.9f;
    const float k = u - 1.0f;
    return 1.0f + k * k * ((c + 1.0f) * k + c);
}

ImU32 fade(ImU32 col, float alpha) {
    const float a = clamp01(alpha) * static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF);
    return (col & ~IM_COL32_A_MASK) |
           (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

ImU32 toU32(const glm::vec4& c) {
    return IM_COL32(static_cast<int>(clamp01(c.r) * 255.0f),
                    static_cast<int>(clamp01(c.g) * 255.0f),
                    static_cast<int>(clamp01(c.b) * 255.0f),
                    static_cast<int>(clamp01(c.a) * 255.0f));
}

ImVec2 textSize(float size, const char* text) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

// Text with a hard black rim, which is what makes a word survive being laid
// over an arena of its own colors. Eight offsets rather than a drop shadow:
// this has to read at 200 px with fighters moving behind it.
//
// `scale` is applied to the emitted *vertices*, never to the font size, and
// that is not a micro-optimisation. ImGui bakes a fresh set of glyphs for every
// distinct size it is asked for, so a stamp animating from 530 px down to 190
// px asks for a few hundred full-size bakes in as many frames — the atlas grows
// without bound and the game stops dead. It did exactly that the first time
// this was written. Baking at one size and moving the vertices costs nothing,
// and the rim scales with the letters for free because it is the same geometry.
void stampText(ImDrawList* dl, ImVec2 center, float baseSize, float scale,
               const char* text, ImU32 col, float alpha) {
    if (alpha <= 0.001f || scale <= 0.001f) {
        return;
    }
    // Rounded so that a window size, not a frame, is what can introduce a new
    // one — a handful of sizes over a session rather than one per frame.
    baseSize = std::max(1.0f, std::round(baseSize));
    ImFont* font = ImGui::GetFont();
    const ImVec2 sz = textSize(baseSize, text);
    const ImVec2 at{center.x - sz.x * 0.5f, center.y - sz.y * 0.5f};
    const float rim = std::max(2.0f, baseSize * 0.045f);
    const int first = dl->VtxBuffer.Size;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            dl->AddText(font, baseSize, {at.x + dx * rim, at.y + dy * rim},
                        fade(kInk, alpha), text);
        }
    }
    dl->AddText(font, baseSize, at, fade(col, alpha), text);
    if (scale != 1.0f) {
        for (int i = first; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.pos.x = center.x + (v.pos.x - center.x) * scale;
            v.pos.y = center.y + (v.pos.y - center.y) * scale;
        }
    }
}

// Black bars top and bottom. Cheap, and they do two jobs: they say "this is a
// sequence, not the match" without a word, and they cover the band a close-up
// camera is most likely to show the edge of the world in.
void letterbox(ImDrawList* dl, const ImVec2& screen, float amount) {
    if (amount <= 0.001f) {
        return;
    }
    const float h = screen.y * 0.11f * clamp01(amount);
    dl->AddRectFilled({0.0f, 0.0f}, {screen.x, h}, kInk);
    dl->AddRectFilled({0.0f, screen.y - h}, {screen.x, screen.y}, kInk);
}

// A slanted name slab: the fighter's name over the blade in their hand, with
// their kimono color capping the leading edge. The slant is what keeps it from
// reading as an ImGui window that forgot its title bar.
void namePlate(ImDrawList* dl, ImVec2 at, float w, float h, const CharacterDef& who,
               const char* blade, bool fromLeft, float alpha) {
    if (alpha <= 0.001f) {
        return;
    }
    const float slant = h * 0.28f;
    const ImVec2 p0{at.x + slant, at.y};
    const ImVec2 p1{at.x + w + slant, at.y};
    const ImVec2 p2{at.x + w, at.y + h};
    const ImVec2 p3{at.x, at.y + h};
    dl->AddQuadFilled(p0, p1, p2, p3, fade(IM_COL32(10, 8, 8, 225), alpha));
    // Crimson underline along the bottom edge, and the fighter's own color on
    // whichever end the plate flew in from.
    dl->AddLine(p3, p2, fade(kCrimson, alpha), std::max(2.0f, h * 0.045f));
    const float capW = h * 0.12f;
    if (fromLeft) {
        dl->AddQuadFilled(p0, {p0.x + capW, p0.y}, {p3.x + capW, p3.y}, p3,
                          fade(toU32(who.look.colors.kimono), alpha));
    } else {
        dl->AddQuadFilled({p1.x - capW, p1.y}, p1, p2, {p2.x - capW, p2.y},
                          fade(toU32(who.look.colors.kimono), alpha));
    }

    const float nameSize = h * 0.44f;
    const float bladeSize = h * 0.2f;
    const float pad = h * 0.22f;
    const ImVec2 nameAt{at.x + pad + slant * 0.5f, at.y + h * 0.14f};
    dl->AddText(ImGui::GetFont(), nameSize, nameAt, fade(kCream, alpha), who.name);
    dl->AddText(ImGui::GetFont(), bladeSize,
                {nameAt.x + 2.0f, nameAt.y + nameSize * 1.02f},
                fade(kCream, alpha * 0.62f), blade);
}

// A decaying knock, used on the two moments something lands. Two frequencies so
// it reads as an impact rather than a wobble.
glm::vec3 shake(float since, float duration, float amount) {
    if (since < 0.0f || since >= duration) {
        return {};
    }
    const float k = 1.0f - since / duration;
    const float decay = k * k * amount;
    return {std::sin(since * 78.0f) * decay, std::sin(since * 51.0f + 1.7f) * decay,
            0.0f};
}

} // namespace

glm::mat4 shotView(const CameraShot& shot) {
    return glm::lookAt(shot.eye, shot.target, glm::vec3(0.0f, 1.0f, 0.0f));
}

// ---------------------------------------------------------------- versus ----

void VersusIntro::begin() {
    m_active = true;
    m_armed = false;
    m_stampPending = true;
    m_t = 0.0f;
}

bool VersusIntro::offerSkip(bool anyDown) {
    if (!m_active) {
        return false;
    }
    if (!m_armed) {
        m_armed = !anyDown;
        return false;
    }
    if (!anyDown || m_t >= kTour) {
        return false;
    }
    // Onto the banner, not past it: the stamp is the thing that says the match
    // has started, and a player who skipped the tour still needs telling.
    m_t = kTour;
    return true;
}

void VersusIntro::update(float dt) {
    if (!m_active) {
        return;
    }
    m_t += dt;
    if (m_t >= kTour + kBanner) {
        m_active = false;
    }
}

bool VersusIntro::takeStampCue() {
    if (m_stampPending && m_t >= kTour) {
        m_stampPending = false;
        return true;
    }
    return false;
}

CameraShot VersusIntro::shot(const Game& game, const CameraShot& gameplay) const {
    // A three-quarter close-up from in front of the fighter, pushing in and
    // drifting across as it holds. Built from the fighter's own facing rather
    // than from world axes, so it stays a close-up whichever way the pair have
    // been placed and whatever a future level does with the spawns.
    auto portrait = [&](int i, float u) -> CameraShot {
        const Player& p = game.player(i);
        const glm::vec2 f = p.forward();
        const glm::vec3 fwd{f.x, 0.0f, f.y};
        const glm::vec3 side{-fwd.z, 0.0f, fwd.x};
        const glm::vec3 head{p.pos.x, p.pos.y + 0.55f, p.pos.z};
        const float dist = glm::mix(3.8f, 3.0f, smooth(u));
        const float across = glm::mix(1.3f, -0.3f, smooth(u));
        return {head + fwd * dist + side * across + glm::vec3{0.0f, 0.30f, 0.0f},
                head};
    };

    if (m_t < kPortrait) {
        return portrait(0, m_t / kPortrait);
    }
    if (m_t < 2.0f * kPortrait) {
        return portrait(1, (m_t - kPortrait) / kPortrait);
    }
    if (m_t < kTour) {
        // Pull back into the shot the fight will be played from, so the sim
        // resuming is the end of a camera move rather than a cut.
        const CameraShot from = portrait(1, 1.0f);
        const float v = smooth((m_t - 2.0f * kPortrait) / kFaceoff);
        return {glm::mix(from.eye, gameplay.eye, v),
                glm::mix(from.target, gameplay.target, v)};
    }
    const glm::vec3 knock = shake(m_t - kTour, 0.22f, 0.20f);
    return {gameplay.eye + knock, gameplay.target};
}

void VersusIntro::draw(const Game& game) const {
    if (!m_active) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 screen = ImGui::GetMainViewport()->Size;
    const float unit = screen.y / 1080.0f; // author against 1080p, scale from there

    // Bars in over the first beat, out as the banner lands and the match starts.
    const float bars = m_t < kTour ? smooth(m_t / 0.3f)
                                   : 1.0f - smooth((m_t - kTour) / 0.28f);
    letterbox(dl, screen, bars);

    const float plateW = 520.0f * unit;
    const float plateH = 108.0f * unit;
    const float margin = 60.0f * unit;

    for (int i = 0; i < 2; ++i) {
        const float appear = static_cast<float>(i) * kPortrait;
        if (m_t < appear) {
            continue;
        }
        const bool fromLeft = i == 0;
        // Down beside the fighter while it is their shot, then up into the
        // corners for the face-off, which is what turns two close-ups into one
        // card with both names on it.
        const float lowY = screen.y * 0.66f;
        const float topY = screen.y * 0.16f + static_cast<float>(i) * plateH * 1.25f;
        const float park = m_t > 2.0f * kPortrait
                               ? smooth((m_t - 2.0f * kPortrait) / (kFaceoff * 0.55f))
                               : 0.0f;
        const float y = glm::mix(lowY, topY, park);

        const float restX = fromLeft ? margin : screen.x - margin - plateW;
        const float offX = fromLeft ? -plateW - margin * 2.0f : screen.x + margin;
        const float slideIn = overshoot((m_t - appear) / 0.32f);
        const float x = glm::mix(offX, restX, slideIn);

        const float alpha =
            m_t > kTour ? 1.0f - smooth((m_t - kTour) / 0.25f) : 1.0f;
        const WeaponDef* held = game.weapon(i);
        namePlate(dl, {x, y}, plateW, plateH, game.character(i),
                  held ? held->name : "unarmed", fromLeft, alpha);
    }

    const ImVec2 center{screen.x * 0.5f, screen.y * 0.5f};

    // VS punches in over the face-off and leaves with the plates.
    if (m_t > 2.0f * kPortrait) {
        const float u = (m_t - 2.0f * kPortrait - 0.12f) / 0.22f;
        if (u > 0.0f) {
            const float pop = glm::mix(3.2f, 1.0f, smooth(u));
            const float alpha =
                m_t > kTour ? 1.0f - smooth((m_t - kTour) / 0.18f) : smooth(u * 2.0f);
            stampText(dl, center, 150.0f * unit, pop, "VS", kCrimson, alpha);
        }
    }

    // FIGHT!, over a match that is already running.
    if (m_t >= kTour) {
        const float b = m_t - kTour;
        const float in = clamp01(b / 0.16f);
        const float out = clamp01((b - 0.45f) / (kBanner - 0.45f));
        const float scale = glm::mix(2.8f, 1.0f, smooth(in)) + smooth(out) * 0.35f;
        stampText(dl, {center.x, screen.y * 0.42f}, 190.0f * unit, scale, "FIGHT!",
                  kGold, std::min(smooth(in * 1.6f), 1.0f - smooth(out)));
    }
}

// ----------------------------------------------------------------- title ----

void TitleIntro::begin() {
    m_active = true;
    m_armed = false;
    m_slamPending = true;
    m_t = 0.0f;
    m_out = 0.0f;
}

// Esc goes straight here rather than through offerSkip: it is unbindable, so
// it can never be a control left held over from the screen before, which is
// the only thing the arming exists to guard against.
void TitleIntro::skip() {
    if (m_active && m_out <= 0.0f) {
        m_out = 0.0001f; // start the handover; it ends when that completes
    }
}

bool TitleIntro::offerSkip(bool anyDown) {
    if (!m_active) {
        return false;
    }
    if (!m_armed) {
        m_armed = !anyDown;
        return false;
    }
    if (!anyDown || m_out > 0.0f) {
        return false;
    }
    skip();
    return true;
}

void TitleIntro::step(float dt, PlayerInput out[2]) {
    out[0] = PlayerInput{};
    out[1] = PlayerInput{};
    if (!m_active) {
        return;
    }

    // An edge lands on exactly the one step whose interval contains it.
    auto edgeAt = [&](float when) { return m_t <= when && when < m_t + dt; };

    if (m_t < kCharge) {
        // Both break stance and run at each other. Sprinting is what makes the
        // approach quick enough to be an opening shot — the roster walks at
        // about a meter a second, which is a deliberate pace to fight at and a
        // very long time to watch.
        out[0].move = {1.0f, 0.0f};
        out[0].sprint = true;
        out[1].move = {-1.0f, 0.0f};
        out[1].sprint = true;
    }
    if (m_t >= kGuard && m_t < kRiposte) {
        out[1].block = true; // the guard that catches the heavy
    }
    if (m_t >= kBreak && m_t < kSettle) {
        // The exchange breaks: the one who swung gives ground, the one who
        // countered holds their guard on him. Both then stand down well before
        // the sequence ends, because the menu freezes the sim where it stops —
        // and a backdrop of two fighters caught mid-stride reads as a hitch.
        out[0].move = {-1.0f, 0.0f};
        out[1].block = true;
    }
    if (edgeAt(kSwing)) {
        out[0].attack = true;
        out[0].attackKind = AttackKind::Heavy;
    }
    if (edgeAt(kRiposte)) {
        // A thrust rather than a cut, and deliberately: a jab cannot sever
        // (game.hpp), so between the caught heavy and this the whole exchange
        // is guaranteed to leave both fighters with all four limbs — which is
        // what lets the diorama be left standing for the menu to come up over
        // instead of having to be rebuilt.
        out[1].attack = true;
        out[1].attackKind = AttackKind::Jab;
    }
    m_t += dt;
    if (m_out > 0.0f) {
        m_out = std::min(1.0f, m_out + dt / 0.35f);
        if (m_out >= 1.0f) {
            m_active = false;
        }
    } else if (m_t >= kEnd) {
        m_out = 0.0001f;
    }
}

bool TitleIntro::takeSlamCue() {
    if (m_slamPending && m_t >= kSlam) {
        m_slamPending = false;
        return true;
    }
    return false;
}

CameraShot TitleIntro::shot(const Game& game, const CameraShot& gameplay) const {
    const glm::vec3 a = game.player(0).pos;
    const glm::vec3 b = game.player(1).pos;
    const glm::vec3 mid = 0.5f * (a + b);

    // Low and close through the charge, rising and pushing in onto the clash,
    // then pulling out and up to the wide shot the logo sits over.
    CameraShot s;
    if (m_t < kSwing) {
        const float u = smooth(m_t / kSwing);
        s.target = {mid.x, glm::mix(0.75f, 1.25f, u), mid.z};
        s.eye = {mid.x - glm::mix(1.6f, 0.4f, u), glm::mix(0.5f, 1.5f, u),
                 glm::mix(5.2f, 4.0f, u)};
    } else if (m_t < kSlam) {
        const float u = smooth((m_t - kSwing) / (kSlam - kSwing));
        s.target = {mid.x, glm::mix(1.25f, 1.4f, u), mid.z};
        s.eye = {mid.x + glm::mix(0.4f, 1.2f, u), glm::mix(1.5f, 2.1f, u),
                 glm::mix(4.0f, 3.4f, u)};
    } else {
        const float u = smooth((m_t - kSlam) / 2.2f);
        s.target = {mid.x, glm::mix(1.4f, 1.9f, u), mid.z};
        s.eye = {mid.x + glm::mix(1.2f, 0.0f, u), glm::mix(2.1f, 4.2f, u),
                 glm::mix(3.4f, 13.0f, u)};
    }
    s.eye += shake(m_t - kSlam, 0.3f, 0.28f);
    // Ease into where the menu's camera already is, so the state change costs
    // no visible cut. By the time m_out completes, the two are the same shot.
    const float v = smooth(m_out);
    return {glm::mix(s.eye, gameplay.eye, v), glm::mix(s.target, gameplay.target, v)};
}

void TitleIntro::draw() const {
    if (!m_active) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 screen = ImGui::GetMainViewport()->Size;
    const float unit = screen.y / 1080.0f;

    // Everything drawn here dims together on the way out, which with the camera
    // already easing to the menu's shot is the whole of the handover.
    const float leaving = 1.0f - smooth(m_out);
    letterbox(dl, screen, std::min(smooth(m_t / 0.4f), leaving));

    const ImVec2 center{screen.x * 0.5f, screen.y * 0.38f};

    if (m_t >= kSlam) {
        const float b = m_t - kSlam;
        // Down from far too big, hard, and then a hairline settle. The rule is
        // the same one the name plates follow: it has to land, not arrive.
        const float scale = glm::mix(5.0f, 1.0f, smooth(clamp01(b / 0.18f))) +
                            std::exp(-b * 14.0f) * std::sin(b * 40.0f) * 0.06f;
        stampText(dl, center, 130.0f * unit, scale, "BUSHIDO", kCream,
                  clamp01(b / 0.1f) * leaving);

        // A crimson rule wiping out from under it, and the tagline behind that.
        const float wipe = smooth((b - 0.16f) / 0.35f);
        if (wipe > 0.0f) {
            const float w = textSize(130.0f * unit, "BUSHIDO").x * 0.55f * wipe;
            const float y = center.y + 78.0f * unit;
            dl->AddRectFilled({center.x - w, y}, {center.x + w, y + 4.0f * unit},
                              fade(kCrimson, wipe * leaving));
        }
        const float sub = smooth((b - 0.4f) / 0.5f);
        if (sub > 0.0f) {
            stampText(dl, {center.x, center.y + 118.0f * unit}, 30.0f * unit, 1.0f,
                      "one cut decides it", kCream, sub * 0.75f * leaving);
        }
    }

    if (m_t >= kPrompt) {
        // Pulsed rather than static: a prompt that does not move is furniture.
        const float pulse = 0.55f + 0.35f * std::sin((m_t - kPrompt) * 3.4f);
        stampText(dl, {screen.x * 0.5f, screen.y * 0.82f}, 28.0f * unit, 1.0f,
                  "press any key", kCream, pulse * leaving);
    }
}
