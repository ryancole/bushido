#include "config.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>

namespace {

constexpr Bind key(int code) { return {Bind::Device::Key, code}; }
constexpr Bind mouse(int code) { return {Bind::Device::Mouse, code}; }

struct BindEntry {
    const char* name;
    Bind bind;
};

// Every bindable control, and the only names that ever appear in the config
// file. Two things fall out of keeping one table: bindName is total (a bind
// that exists has a name, so no "unknown key" rendering path), and the options
// screen's capture is just a scan of this array — a control that isn't listed
// simply can't be bound.
//
// Escape is deliberately absent. It's the hardwired back-out key, so letting a
// player bind it to a fighting control would strand them mid-match.
constexpr BindEntry kBindTable[] = {
    {"A", key(GLFW_KEY_A)},
    {"B", key(GLFW_KEY_B)},
    {"C", key(GLFW_KEY_C)},
    {"D", key(GLFW_KEY_D)},
    {"E", key(GLFW_KEY_E)},
    {"F", key(GLFW_KEY_F)},
    {"G", key(GLFW_KEY_G)},
    {"H", key(GLFW_KEY_H)},
    {"I", key(GLFW_KEY_I)},
    {"J", key(GLFW_KEY_J)},
    {"K", key(GLFW_KEY_K)},
    {"L", key(GLFW_KEY_L)},
    {"M", key(GLFW_KEY_M)},
    {"N", key(GLFW_KEY_N)},
    {"O", key(GLFW_KEY_O)},
    {"P", key(GLFW_KEY_P)},
    {"Q", key(GLFW_KEY_Q)},
    {"R", key(GLFW_KEY_R)},
    {"S", key(GLFW_KEY_S)},
    {"T", key(GLFW_KEY_T)},
    {"U", key(GLFW_KEY_U)},
    {"V", key(GLFW_KEY_V)},
    {"W", key(GLFW_KEY_W)},
    {"X", key(GLFW_KEY_X)},
    {"Y", key(GLFW_KEY_Y)},
    {"Z", key(GLFW_KEY_Z)},
    {"0", key(GLFW_KEY_0)},
    {"1", key(GLFW_KEY_1)},
    {"2", key(GLFW_KEY_2)},
    {"3", key(GLFW_KEY_3)},
    {"4", key(GLFW_KEY_4)},
    {"5", key(GLFW_KEY_5)},
    {"6", key(GLFW_KEY_6)},
    {"7", key(GLFW_KEY_7)},
    {"8", key(GLFW_KEY_8)},
    {"9", key(GLFW_KEY_9)},
    {"F1", key(GLFW_KEY_F1)},
    {"F2", key(GLFW_KEY_F2)},
    {"F3", key(GLFW_KEY_F3)},
    {"F4", key(GLFW_KEY_F4)},
    {"F5", key(GLFW_KEY_F5)},
    {"F6", key(GLFW_KEY_F6)},
    {"F7", key(GLFW_KEY_F7)},
    {"F8", key(GLFW_KEY_F8)},
    {"F9", key(GLFW_KEY_F9)},
    {"F10", key(GLFW_KEY_F10)},
    {"F11", key(GLFW_KEY_F11)},
    {"F12", key(GLFW_KEY_F12)},
    {"Space", key(GLFW_KEY_SPACE)},
    {"Enter", key(GLFW_KEY_ENTER)},
    {"Tab", key(GLFW_KEY_TAB)},
    {"Backspace", key(GLFW_KEY_BACKSPACE)},
    {"Insert", key(GLFW_KEY_INSERT)},
    {"Delete", key(GLFW_KEY_DELETE)},
    {"Left", key(GLFW_KEY_LEFT)},
    {"Right", key(GLFW_KEY_RIGHT)},
    {"Up", key(GLFW_KEY_UP)},
    {"Down", key(GLFW_KEY_DOWN)},
    {"Page Up", key(GLFW_KEY_PAGE_UP)},
    {"Page Down", key(GLFW_KEY_PAGE_DOWN)},
    {"Home", key(GLFW_KEY_HOME)},
    {"End", key(GLFW_KEY_END)},
    {"Caps Lock", key(GLFW_KEY_CAPS_LOCK)},
    {"Left Shift", key(GLFW_KEY_LEFT_SHIFT)},
    {"Left Ctrl", key(GLFW_KEY_LEFT_CONTROL)},
    {"Left Alt", key(GLFW_KEY_LEFT_ALT)},
    {"Left Super", key(GLFW_KEY_LEFT_SUPER)},
    {"Right Shift", key(GLFW_KEY_RIGHT_SHIFT)},
    {"Right Ctrl", key(GLFW_KEY_RIGHT_CONTROL)},
    {"Right Alt", key(GLFW_KEY_RIGHT_ALT)},
    {"Right Super", key(GLFW_KEY_RIGHT_SUPER)},
    {"Apostrophe", key(GLFW_KEY_APOSTROPHE)},
    {"Comma", key(GLFW_KEY_COMMA)},
    {"Minus", key(GLFW_KEY_MINUS)},
    {"Period", key(GLFW_KEY_PERIOD)},
    {"Slash", key(GLFW_KEY_SLASH)},
    {"Semicolon", key(GLFW_KEY_SEMICOLON)},
    {"Equal", key(GLFW_KEY_EQUAL)},
    {"Left Bracket", key(GLFW_KEY_LEFT_BRACKET)},
    {"Backslash", key(GLFW_KEY_BACKSLASH)},
    {"Right Bracket", key(GLFW_KEY_RIGHT_BRACKET)},
    {"Grave", key(GLFW_KEY_GRAVE_ACCENT)},
    {"Num 0", key(GLFW_KEY_KP_0)},
    {"Num 1", key(GLFW_KEY_KP_1)},
    {"Num 2", key(GLFW_KEY_KP_2)},
    {"Num 3", key(GLFW_KEY_KP_3)},
    {"Num 4", key(GLFW_KEY_KP_4)},
    {"Num 5", key(GLFW_KEY_KP_5)},
    {"Num 6", key(GLFW_KEY_KP_6)},
    {"Num 7", key(GLFW_KEY_KP_7)},
    {"Num 8", key(GLFW_KEY_KP_8)},
    {"Num 9", key(GLFW_KEY_KP_9)},
    {"Num Period", key(GLFW_KEY_KP_DECIMAL)},
    {"Num Divide", key(GLFW_KEY_KP_DIVIDE)},
    {"Num Multiply", key(GLFW_KEY_KP_MULTIPLY)},
    {"Num Minus", key(GLFW_KEY_KP_SUBTRACT)},
    {"Num Plus", key(GLFW_KEY_KP_ADD)},
    {"Num Enter", key(GLFW_KEY_KP_ENTER)},
    {"Mouse 1", mouse(GLFW_MOUSE_BUTTON_1)},
    {"Mouse 2", mouse(GLFW_MOUSE_BUTTON_2)},
    {"Mouse 3", mouse(GLFW_MOUSE_BUTTON_3)},
    {"Mouse 4", mouse(GLFW_MOUSE_BUTTON_4)},
    {"Mouse 5", mouse(GLFW_MOUSE_BUTTON_5)},
};

struct ActionInfo {
    const char* label; // options-screen row label
    const char* tomlKey;
    const char* hint;
};

// Indexed by Action. The TOML keys are the on-disk contract: rows above can be
// reordered or relabeled freely, but a key here is never renamed.
constexpr ActionInfo kActions[kActionCount] = {
    {"Move Left", "move_left", "Walk left"},
    {"Move Right", "move_right", "Walk right"},
    {"Move Away", "move_away", "Walk into the screen"},
    {"Move Toward", "move_toward", "Walk toward the camera"},
    {"Jump", "jump", "Leave the ground"},
    {"Block", "block", "Hold to guard - catch a blow to open a riposte"},
    {"Crouch", "crouch", "Hold to duck; swings ride a lower lane"},
    {"Attack", "attack", "Tap to cut, hold and release to crush"},
    {"Jab", "jab", "A short thrust - pierces, never severs"},
};

} // namespace

Keybinds defaultKeybinds() {
    Keybinds k;
    k[Action::MoveLeft] = key(GLFW_KEY_A);
    k[Action::MoveRight] = key(GLFW_KEY_D);
    k[Action::MoveAway] = key(GLFW_KEY_W);
    k[Action::MoveToward] = key(GLFW_KEY_S);
    k[Action::Jump] = key(GLFW_KEY_SPACE);
    k[Action::Block] = key(GLFW_KEY_LEFT_SHIFT);
    k[Action::Crouch] = key(GLFW_KEY_LEFT_CONTROL);
    k[Action::Attack] = mouse(GLFW_MOUSE_BUTTON_LEFT);
    k[Action::Jab] = mouse(GLFW_MOUSE_BUTTON_RIGHT);
    return k;
}

Keybinds defaultKeybinds2() {
    // The right-hand end of the board: arrows to move, the comma/period/slash
    // cluster and the right modifiers for everything else. Disjoint from
    // player 1's set by construction, and no mouse — one mouse cannot serve
    // two fighters. Deliberately not the keypad, which half of laptops don't
    // have; every key here is on every keyboard.
    Keybinds k;
    k[Action::MoveLeft] = key(GLFW_KEY_LEFT);
    k[Action::MoveRight] = key(GLFW_KEY_RIGHT);
    k[Action::MoveAway] = key(GLFW_KEY_UP);
    k[Action::MoveToward] = key(GLFW_KEY_DOWN);
    k[Action::Jump] = key(GLFW_KEY_SLASH);
    k[Action::Block] = key(GLFW_KEY_RIGHT_SHIFT);
    k[Action::Crouch] = key(GLFW_KEY_RIGHT_CONTROL);
    k[Action::Attack] = key(GLFW_KEY_PERIOD);
    k[Action::Jab] = key(GLFW_KEY_COMMA);
    return k;
}

const char* actionName(Action a) { return kActions[static_cast<int>(a)].label; }
const char* actionKey(Action a) { return kActions[static_cast<int>(a)].tomlKey; }
const char* actionHint(Action a) { return kActions[static_cast<int>(a)].hint; }

const char* bindName(Bind b) {
    for (const BindEntry& e : kBindTable) {
        if (e.bind == b) {
            return e.name;
        }
    }
    return "-"; // unreachable for binds that came from the table
}

bool parseBind(std::string_view name, Bind& out) {
    for (const BindEntry& e : kBindTable) {
        if (name == e.name) {
            out = e.bind;
            return true;
        }
    }
    return false;
}

bool bindHeld(GLFWwindow* window, Bind b) {
    return b.device == Bind::Device::Mouse
               ? glfwGetMouseButton(window, b.code) == GLFW_PRESS
               : glfwGetKey(window, b.code) == GLFW_PRESS;
}

bool pollAnyBind(GLFWwindow* window, Bind& out) {
    for (const BindEntry& e : kBindTable) {
        if (bindHeld(window, e.bind)) {
            out = e.bind;
            return true;
        }
    }
    return false;
}

const std::filesystem::path& configPath() {
    static const std::filesystem::path path = [] {
        char* appdata = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
            std::filesystem::path p =
                std::filesystem::path(appdata) / "bushido" / "bushido.toml";
            std::free(appdata);
            return p;
        }
        return std::filesystem::path("bushido.toml");
    }();
    return path;
}

bool loadConfig(Settings& settings) {
    toml::table tbl;
    try {
        std::ifstream file(configPath());
        if (!file) {
            return false; // no settings yet; the caller's defaults stand
        }
        tbl = toml::parse(file);
    } catch (const toml::parse_error& e) {
        std::fprintf(stderr, "config: %s at line %u - using defaults\n", e.what(),
                     e.source().begin.line);
        return false;
    }

    // Sections are read independently: a file predating one of them (or with a
    // section deleted) keeps the defaults for it and loads the rest.
    auto readKeybinds = [&](const char* table, Keybinds& out) {
        const toml::table* keys = tbl[table].as_table();
        if (!keys) {
            return;
        }
        // Per-entry recovery too: a stale or misspelled name loses that one
        // binding to its default rather than the whole section.
        for (int i = 0; i < kActionCount; ++i) {
            const Action a = static_cast<Action>(i);
            std::optional<std::string_view> name =
                (*keys)[actionKey(a)].value<std::string_view>();
            if (!name) {
                continue;
            }
            Bind b;
            if (parseBind(*name, b)) {
                out[a] = b;
            } else {
                std::fprintf(stderr, "config: %s.%s has unknown control \"%.*s\"\n",
                             table, actionKey(a), static_cast<int>(name->size()),
                             name->data());
            }
        }
    };
    readKeybinds("keybinds", settings.keybinds);
    readKeybinds("keybinds_p2", settings.keybinds2);

    if (const toml::table* audio = tbl["audio"].as_table()) {
        // Out-of-range levels are clamped rather than refused — a hand-edited
        // 2 or -1 is obvious intent, not a reason to drop the setting.
        auto readLevel = [&](const char* key, float& out) {
            std::optional<double> v = (*audio)[key].value<double>();
            if (!v) {
                // TOML tells 1 from 1.0 apart; accept a hand-written integer.
                if (std::optional<std::int64_t> i =
                        (*audio)[key].value<std::int64_t>()) {
                    v = static_cast<double>(*i);
                }
            }
            if (v) {
                out = std::clamp(static_cast<float>(*v), 0.0f, 1.0f);
            }
        };
        readLevel("music", settings.audio.music);
        readLevel("sfx", settings.audio.sfx);
    }
    return true;
}

bool saveConfig(const Settings& settings) {
    std::error_code ec;
    std::filesystem::create_directories(configPath().parent_path(), ec);

    std::ofstream out(configPath(), std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "config: could not write %s\n",
                     configPath().filename().string().c_str());
        return false;
    }
    // Emitted by hand rather than through toml++'s serializer: that path drops
    // comments and sorts keys alphabetically, and for nine lines a header
    // explaining the file plus the roster's own order is worth more than
    // sharing code with the parser. The output is still plain TOML.
    out << "# bushido settings.\n"
           "#\n"
           "# Control names are the ones the options screen shows (\"Left Shift\",\n"
           "# \"Mouse 1\", \"Num Plus\"). An unrecognized name falls back to that\n"
           "# action's default. Volumes run 0 (silent) to 1 (as synthesized).\n"
           "# Delete this file, or any single section, to reset it.\n"
           "#\n"
           "# [keybinds] is player 1, [keybinds_p2] the second local fighter.\n"
           "# Both are live at once in a local versus, so they should not\n"
           "# share a control - the options screen keeps them apart for you.\n";
    auto writeKeybinds = [&](const char* table, const Keybinds& keys) {
        out << "\n[" << table << "]\n";
        for (int i = 0; i < kActionCount; ++i) {
            const Action a = static_cast<Action>(i);
            out << std::left << std::setw(12) << actionKey(a) << " = \""
                << bindName(keys[a]) << "\"\n";
        }
    };
    writeKeybinds("keybinds", settings.keybinds);
    writeKeybinds("keybinds_p2", settings.keybinds2);

    out << "\n[audio]\n" << std::fixed << std::setprecision(2);
    out << std::left << std::setw(12) << "music" << " = " << settings.audio.music
        << "\n";
    out << std::left << std::setw(12) << "sfx" << " = " << settings.audio.sfx << "\n";
    return out.good();
}
