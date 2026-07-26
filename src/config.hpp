#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

struct GLFWwindow;

// Player-editable settings, persisted as TOML. Keybinds are local input
// mapping only — nothing here reaches the sim, so rebinding can never desync a
// peer the way a stat change would.

// One bound control. Keys and mouse buttons are separate GLFW numbering spaces
// (GLFW_MOUSE_BUTTON_LEFT is 0, which is also a valid key code), so the device
// has to travel with the code.
struct Bind {
    enum class Device : std::uint8_t { Key, Mouse };
    Device device = Device::Key;
    int code = 0; // GLFW_KEY_* or GLFW_MOUSE_BUTTON_*

    bool operator==(const Bind&) const = default;
};

// Every rebindable action, in the order the options screen lists them. The
// enum order is display order; the TOML key (actionKey) is what's stable on
// disk, so rows can be reordered here without invalidating saved configs.
enum class Action {
    MoveLeft,
    MoveRight,
    MoveAway,
    MoveToward,
    Jump,
    Block,
    Crouch,
    Attack,
    Jab,
    Count,
};
constexpr int kActionCount = static_cast<int>(Action::Count);

struct Keybinds {
    Bind binds[kActionCount];

    Bind& operator[](Action a) { return binds[static_cast<int>(a)]; }
    const Bind& operator[](Action a) const { return binds[static_cast<int>(a)]; }
};

// Two sets, one per local fighter. The defaults deliberately share no control:
// in a local versus both are live at once, so an overlap would move both
// fighters on one key. Player 1 keeps the mouse; player 2 gets the right-hand
// side of the keyboard.
Keybinds defaultKeybinds();
Keybinds defaultKeybinds2();

// Mixer levels, 0..1. Both default to 1 — the levels the effects and tracks
// were synthesized against — so a fresh install sounds exactly as it always
// has, and the sliders only ever cut.
struct AudioSettings {
    float music = 1.0f;
    float sfx = 1.0f;
};

// Everything the player can change, and the whole of what the config file
// holds: one struct per options-screen section.
struct Settings {
    Keybinds keybinds = defaultKeybinds();   // player 1
    Keybinds keybinds2 = defaultKeybinds2(); // player 2, local versus only
    AudioSettings audio;
};

const char* actionName(Action a); // "Move Left" — the options-screen label
const char* actionKey(Action a);  // "move_left" — the TOML key, never changes
const char* actionHint(Action a); // what the control does, for the options screen

// Display name of a bind ("A", "Space", "Left Shift", "Mouse 1"), and the
// reverse lookup. Both go through one curated table of bindable controls, so a
// bind that exists always has a name — see config.cpp.
const char* bindName(Bind b);
bool parseBind(std::string_view name, Bind& out);

// Scan the bindable-control table for something currently held, for the
// options screen's "press a control" capture. Returns false if nothing is
// down; a control outside the table simply isn't bindable.
bool pollAnyBind(GLFWwindow* window, Bind& out);
bool bindHeld(GLFWwindow* window, Bind b);

// %APPDATA%/bushido/bushido.toml (cwd if APPDATA is unset).
const std::filesystem::path& configPath();
// A missing or malformed file leaves `settings` untouched and returns false, so
// the caller's defaults stand. Within a readable file, recovery is per-entry:
// an absent section, an unknown key, or an unparsable value costs only that one
// setting its default, never the whole load.
bool loadConfig(Settings& settings);
bool saveConfig(const Settings& settings);
