#ifndef EMULATOR_SRC_LOCAL_CONTROLLER_HOTKEYS_H_
#define EMULATOR_SRC_LOCAL_CONTROLLER_HOTKEYS_H_
// Controller hotkeys: hold the modifier button (default Create/Back/Share), then press another
// button.
//
// The bindings live in hotkeys.ini next to the emulator (KYTY_HOTKEYS_FILE elsewhere; written with
// the defaults when missing); KYTY_HOTKEY_MODIFIER=<button> overrides its modifier for one run (the
// launcher's "Hotkey button"). The modifier can be changed while playing: modifier + R3, then press
// the new one (saved to the file).
//
// Default chords (modifier +):
//   Options/Start (hold 1 s)  quit the game (closes the emulator's window, back to the launcher)
//   Triangle/Y                full screen on/off
//   Square/X                  pause/resume
//   Circle/B                  performance overlay: off, frame rate, detailed (as F8)
//   Cross/A                   frame generation on/off (DLSS-G or FSR 3)
//   D-pad up/down             FSR sharpness +/- 0.1
//   L3                        show the hotkeys
//   R3                        choose another modifier button
// Buttons pressed while the modifier is held do not reach the game. A modifier the game also uses
// (e.g. L3) still reaches it when tapped alone: its press is sent on its release.
//
// Button names (hotkeys.ini, KYTY_HOTKEY_MODIFIER): SDL's (a, b, x, y, back, guide, start,
// leftstick, rightstick, leftshoulder, rightshoulder, dpup, dpdown, dpleft, dpright, misc1,
// paddle1-4, touchpad) or PlayStation's (Cross, Circle, Square, Triangle, Create, Share, PS,
// Options, L1, R1, L3, R3, Touchpad, Up, Down, Left, Right, Mute).
#include <cstdint>
#include <string>

union SDL_Event;

namespace Local::ControllerHotkeys {

// What the chords do: the window's own actions (window.cpp).
struct Actions {
	void (*quit)()              = nullptr;
	void (*toggle_fullscreen)() = nullptr;
	void (*toggle_pause)()      = nullptr;
	void (*cycle_overlay)()     = nullptr;
};

// The window's thread, once, before its events: reads (or writes) hotkeys.ini.
void Initialize(const Actions& actions);
// A controller button event, before it goes to the game: true when the hotkeys took it (the game
// does not see it). `pad_button`: the game's button for it (0: none), for a modifier tapped alone.
[[nodiscard]] bool OnButton(int32_t controller, int sdl_button, bool down, uint32_t pad_button);
// An event of the hotkeys' own (their timers): true when it was one.
[[nodiscard]] bool OnEvent(const SDL_Event& event);
// The bindings, for the launcher's notes and the help notice.
[[nodiscard]] std::string Describe();

} // namespace Local::ControllerHotkeys

#endif // EMULATOR_SRC_LOCAL_CONTROLLER_HOTKEYS_H_
