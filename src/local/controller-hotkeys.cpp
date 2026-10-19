#include "controller-hotkeys.h"

#include "SDL_events.h"
#include "SDL_filesystem.h"
#include "SDL_gamecontroller.h"
#include "SDL_stdinc.h"
#include "SDL_timer.h"
#include "frame-gen.h"
#include "fsr.h"
#include "libs/controller.h"
#include "loader/demonsSoulsWarp.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Local::ControllerHotkeys {

namespace {

enum class Action : uint8_t {
	Quit,
	Fullscreen,
	Pause,
	Overlay,
	FrameGen,
	SharpnessUp,
	SharpnessDown,
	Help,
	Rebind,
	Count
};
constexpr auto ActionCount = static_cast<size_t>(Action::Count);

struct ActionInfo {
	const char* key;   // hotkeys.ini
	const char* label; // the help notice
	int         button;
};
constexpr std::array<ActionInfo, ActionCount> Infos {{
    {"Quit", "quit the game (hold 1 s)", SDL_CONTROLLER_BUTTON_START},
    {"Fullscreen", "full screen on/off", SDL_CONTROLLER_BUTTON_Y},
    {"Pause", "pause/resume", SDL_CONTROLLER_BUTTON_X},
    {"Overlay", "performance overlay", SDL_CONTROLLER_BUTTON_B},
    {"FrameGen", "frame generation on/off", SDL_CONTROLLER_BUTTON_A},
    {"SharpnessUp", "FSR sharpness +", SDL_CONTROLLER_BUTTON_DPAD_UP},
    {"SharpnessDown", "FSR sharpness -", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"Help", "show these hotkeys", SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {"RebindModifier", "choose another hotkey button", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
}};
constexpr int                                 DefaultModifier = SDL_CONTROLLER_BUTTON_BACK;

// Names a button goes by (lower case, without spaces, dashes or slashes), PlayStation's and SDL's.
struct Name {
	std::string_view name;
	int              button;
};
constexpr Name Names[] {
    {"cross", SDL_CONTROLLER_BUTTON_A},
    {"a", SDL_CONTROLLER_BUTTON_A},
    {"circle", SDL_CONTROLLER_BUTTON_B},
    {"b", SDL_CONTROLLER_BUTTON_B},
    {"square", SDL_CONTROLLER_BUTTON_X},
    {"x", SDL_CONTROLLER_BUTTON_X},
    {"triangle", SDL_CONTROLLER_BUTTON_Y},
    {"y", SDL_CONTROLLER_BUTTON_Y},
    {"create", SDL_CONTROLLER_BUTTON_BACK},
    {"share", SDL_CONTROLLER_BUTTON_BACK},
    {"select", SDL_CONTROLLER_BUTTON_BACK},
    {"view", SDL_CONTROLLER_BUTTON_BACK},
    {"back", SDL_CONTROLLER_BUTTON_BACK},
    {"ps", SDL_CONTROLLER_BUTTON_GUIDE},
    {"home", SDL_CONTROLLER_BUTTON_GUIDE},
    {"guide", SDL_CONTROLLER_BUTTON_GUIDE},
    {"options", SDL_CONTROLLER_BUTTON_START},
    {"menu", SDL_CONTROLLER_BUTTON_START},
    {"start", SDL_CONTROLLER_BUTTON_START},
    {"l3", SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {"leftstick", SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {"r3", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {"rightstick", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {"l1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"lb", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"leftshoulder", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"r1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"rb", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"rightshoulder", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"up", SDL_CONTROLLER_BUTTON_DPAD_UP},
    {"dpup", SDL_CONTROLLER_BUTTON_DPAD_UP},
    {"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"dpdown", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {"dpleft", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {"dpright", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {"mute", SDL_CONTROLLER_BUTTON_MISC1},
    {"misc1", SDL_CONTROLLER_BUTTON_MISC1},
    {"touchpad", SDL_CONTROLLER_BUTTON_TOUCHPAD},
    {"paddle1", SDL_CONTROLLER_BUTTON_PADDLE1},
    {"paddle2", SDL_CONTROLLER_BUTTON_PADDLE2},
    {"paddle3", SDL_CONTROLLER_BUTTON_PADDLE3},
    {"paddle4", SDL_CONTROLLER_BUTTON_PADDLE4},
};

int ButtonFromName(std::string_view text) {
	std::string key;
	for (const char c: text) {
		if (c == ' ' || c == '-' || c == '_' || c == '/' || c == '\t') continue;
		key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	for (const auto& name: Names) {
		if (name.name == key) return name.button;
	}
	return SDL_CONTROLLER_BUTTON_INVALID;
}

// The name shown and saved: PlayStation's where it has one.
const char* DisplayName(int button) {
	switch (button) {
		case SDL_CONTROLLER_BUTTON_A: return "Cross";
		case SDL_CONTROLLER_BUTTON_B: return "Circle";
		case SDL_CONTROLLER_BUTTON_X: return "Square";
		case SDL_CONTROLLER_BUTTON_Y: return "Triangle";
		case SDL_CONTROLLER_BUTTON_BACK: return "Create";
		case SDL_CONTROLLER_BUTTON_GUIDE: return "PS";
		case SDL_CONTROLLER_BUTTON_START: return "Options";
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "L1";
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return "Up";
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "Down";
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "Left";
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "Right";
		case SDL_CONTROLLER_BUTTON_MISC1: return "Mute";
		case SDL_CONTROLLER_BUTTON_TOUCHPAD: return "Touchpad";
		case SDL_CONTROLLER_BUTTON_PADDLE1: return "Paddle1";
		case SDL_CONTROLLER_BUTTON_PADDLE2: return "Paddle2";
		case SDL_CONTROLLER_BUTTON_PADDLE3: return "Paddle3";
		case SDL_CONTROLLER_BUTTON_PADDLE4: return "Paddle4";
		default: return "?";
	}
}

// The hotkeys' timers come back as events of their own type, on the window's thread.
enum TimerCode : int32_t { QuitHeld = 1, TapRelease = 2, RebindTimeout = 3, QuitChordHeld = 4 };

struct Pad {
	bool                                        modifier_down = false;
	bool                                        chord_used    = false;
	std::array<bool, SDL_CONTROLLER_BUTTON_MAX> swallowed {};
	std::array<bool, SDL_CONTROLLER_BUTTON_MAX> held {};            // every button down now
	bool                                        quit_armed = false; // the quit shortcut is held
};

// QuitChord (hotkeys.ini, the launcher records it): a quit shortcut of its own, one button held
// (2 seconds) or two held together (1 second), whatever the modifier. Its buttons still reach the
// game: a button the game does not use is the best choice for one alone.
std::vector<int> g_quit_chord;
uint32_t         g_quit_chord_token = 0;
uint32_t         QuitChordMs() {
	return g_quit_chord.size() == 1 ? 2000u : 1000u;
}

Actions                          g_actions;
int                              g_modifier = DefaultModifier;
std::array<int, ActionCount>     g_bind {};
std::filesystem::path            g_file;
uint32_t                         g_event_type = static_cast<uint32_t>(-1);
std::unordered_map<int32_t, Pad> g_pads;
bool                             g_rebinding       = false;
uint32_t                         g_rebind_token    = 0;
int32_t                          g_quit_controller = -1;
uint32_t                         g_quit_token      = 0;

void Notice(const std::string& text, int seconds = 3) {
	std::printf("Hotkeys: %s\n", text.c_str());
	std::fflush(stdout);
	Loader::DemonsSoulsWarp::ShowNotice(text, seconds);
}

struct TimerPayload {
	int32_t code;
	int32_t a;
	int32_t b;
};

Uint32 TimerFired(Uint32 /*interval*/, void* param) {
	auto*     payload = static_cast<TimerPayload*>(param);
	SDL_Event event {};
	event.type       = g_event_type;
	event.user.code  = payload->code;
	event.user.data1 = reinterpret_cast<void*>(static_cast<intptr_t>(payload->a));
	event.user.data2 = reinterpret_cast<void*>(static_cast<intptr_t>(payload->b));
	delete payload;
	SDL_PushEvent(&event);
	return 0; // (once)
}

void After(uint32_t ms, int32_t code, int32_t a, int32_t b) {
	if (g_event_type == static_cast<uint32_t>(-1)) return;
	auto* payload = new TimerPayload {code, a, b};
	if (SDL_AddTimer(ms, TimerFired, payload) == 0) delete payload;
}

std::string Lower(std::string_view text) {
	std::string out;
	for (const char c: text)
		out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

std::string_view Trim(std::string_view text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
		text.remove_prefix(1);
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
		text.remove_suffix(1);
	return text;
}

void Save() {
	std::ofstream out(g_file, std::ios::trunc);
	if (!out) {
		std::printf("Hotkeys: cannot write %s\n", g_file.string().c_str());
		return;
	}
	out << "# KytyPS5 controller hotkeys: hold Modifier, then press a button.\n"
	       "# Names: Cross Circle Square Triangle Create PS Options L1 R1 L3 R3 Up Down Left Right "
	       "Touchpad Mute Paddle1-4\n"
	       "# (or SDL's: a b x y back guide start leftshoulder rightshoulder leftstick rightstick "
	       "dpup ...). Empty: unbound.\n"
	       "# In the game, Modifier + R3 then a button changes Modifier (saved here).\n";
	out << "Modifier=" << DisplayName(g_modifier) << "\n";
	out << "QuitChord=";
	for (size_t i = 0; i < g_quit_chord.size(); ++i)
		out << (i != 0 ? "+" : "") << DisplayName(g_quit_chord[i]);
	out << "\n";
	for (size_t i = 0; i < ActionCount; ++i) {
		out << Infos[i].key << "="
		    << (g_bind[i] == SDL_CONTROLLER_BUTTON_INVALID ? "" : DisplayName(g_bind[i])) << "\n";
	}
}

void Load() {
	for (size_t i = 0; i < ActionCount; ++i)
		g_bind[i] = Infos[i].button;
	g_modifier = DefaultModifier;
	g_quit_chord.clear();
	std::ifstream in(g_file);
	if (!in) {
		Save();
		std::printf("Hotkeys: %s written with the defaults\n", g_file.string().c_str());
		return;
	}
	std::string line;
	bool        first = true;
	while (std::getline(in, line)) {
		std::string_view text = line;
		// (A UTF-8 byte order mark, as Windows editors may save it.)
		if (first && text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
		first = false;
		if (const auto comment = text.find_first_of("#;"); comment != std::string_view::npos)
			text = text.substr(0, comment);
		const auto equals = text.find('=');
		if (equals == std::string_view::npos) continue;
		const auto key   = Lower(Trim(text.substr(0, equals)));
		const auto value = Trim(text.substr(equals + 1));
		if (key == "quitchord") {
			// One or two buttons, "+" between them (e.g. Create+Options, Back).
			std::string_view rest = value;
			while (!rest.empty() && g_quit_chord.size() < 2) {
				const auto plus  = rest.find('+');
				const auto part  = Trim(rest.substr(0, plus));
				const int  found = ButtonFromName(part);
				if (found != SDL_CONTROLLER_BUTTON_INVALID &&
				    std::find(g_quit_chord.begin(), g_quit_chord.end(), found) ==
				        g_quit_chord.end())
					g_quit_chord.push_back(found);
				else if (!part.empty())
					std::printf("Hotkeys: quitchord: '%s' is no button name\n",
					            std::string(part).c_str());
				rest = plus == std::string_view::npos ? std::string_view {} : rest.substr(plus + 1);
			}
			continue;
		}
		const int button = value.empty() ? SDL_CONTROLLER_BUTTON_INVALID : ButtonFromName(value);
		if (!value.empty() && button == SDL_CONTROLLER_BUTTON_INVALID) {
			std::printf("Hotkeys: %s: '%s' is no button name\n", key.c_str(),
			            std::string(value).c_str());
			continue;
		}
		if (key == "modifier") {
			if (button != SDL_CONTROLLER_BUTTON_INVALID) g_modifier = button;
			continue;
		}
		for (size_t i = 0; i < ActionCount; ++i) {
			if (Lower(Infos[i].key) == key) g_bind[i] = button;
		}
	}
}

int ActionFor(int button) {
	for (size_t i = 0; i < ActionCount; ++i) {
		if (g_bind[i] == button && button != g_modifier) return static_cast<int>(i);
	}
	return -1;
}

void SetModifier(int button, bool save) {
	g_modifier = button;
	// (A chord on the new modifier would never fire: it is unbound.)
	for (auto& bound: g_bind) {
		if (bound == button) bound = SDL_CONTROLLER_BUTTON_INVALID;
	}
	if (save) Save();
}

void Run(Action action, int32_t controller) {
	using namespace Libs::Graphics;
	switch (action) {
		case Action::Quit:
			g_quit_controller = controller;
			After(1000, QuitHeld, controller, static_cast<int32_t>(++g_quit_token));
			Notice("Keep holding to quit the game...", 1);
			break;
		case Action::Fullscreen:
			if (g_actions.toggle_fullscreen != nullptr) g_actions.toggle_fullscreen();
			break;
		case Action::Pause:
			if (g_actions.toggle_pause != nullptr) g_actions.toggle_pause();
			break;
		case Action::Overlay:
			if (g_actions.cycle_overlay != nullptr) g_actions.cycle_overlay();
			break;
		case Action::FrameGen: {
			const char* backend = FrameGen::BackendName();
			if (backend == nullptr) {
				Notice("Frame generation is not running (choose it in the launcher)");
				break;
			}
			FrameGen::SetUserEnabled(!FrameGen::UserEnabled());
			Notice(std::string(backend) + " frame generation " +
			       (FrameGen::UserEnabled() ? "on" : "off"));
			break;
		}
		case Action::SharpnessUp:
		case Action::SharpnessDown: {
			if (!FrameGen::Fsr::UpscalerReady()) {
				Notice("Sharpness is the FSR upscaler's (launcher: Upscaler -> FSR)");
				break;
			}
			const float step = action == Action::SharpnessUp ? 0.1f : -0.1f;
			FrameGen::Fsr::SetSharpness(FrameGen::Fsr::Sharpness() + step);
			char text[64];
			std::snprintf(text, sizeof(text), "FSR sharpness %.2f",
			              static_cast<double>(FrameGen::Fsr::Sharpness()));
			Notice(text, 2);
			break;
		}
		case Action::Help: Notice(Describe(), 10); break;
		case Action::Rebind:
			g_rebinding = true;
			After(6000, RebindTimeout, 0, static_cast<int32_t>(++g_rebind_token));
			Notice("Press the button to use as the hotkey button (6 s)...", 6);
			break;
		case Action::Count: break;
	}
}

} // namespace

void Initialize(const Actions& actions) {
	g_actions = actions;
	if (const char* file = std::getenv("KYTY_HOTKEYS_FILE"); file != nullptr && *file != '\0') {
		g_file = file;
	} else if (char* base = SDL_GetBasePath(); base != nullptr) {
		g_file = std::filesystem::path(base) / "hotkeys.ini";
		SDL_free(base);
	} else {
		g_file = "hotkeys.ini";
	}
	Load();
	if (const char* modifier = std::getenv("KYTY_HOTKEY_MODIFIER");
	    modifier != nullptr && *modifier != '\0') {
		if (const int button = ButtonFromName(modifier); button != SDL_CONTROLLER_BUTTON_INVALID) {
			SetModifier(button, false);
		} else {
			std::printf("Hotkeys: KYTY_HOTKEY_MODIFIER=%s is no button name; %s kept\n", modifier,
			            DisplayName(g_modifier));
		}
	}
	SetModifier(g_modifier, false);
	g_event_type = SDL_RegisterEvents(1);
	std::printf("Hotkeys: hold %s + a button (%s; %s + %s shows them)\n", DisplayName(g_modifier),
	            g_file.string().c_str(), DisplayName(g_modifier),
	            g_bind[static_cast<size_t>(Action::Help)] != SDL_CONTROLLER_BUTTON_INVALID
	                ? DisplayName(g_bind[static_cast<size_t>(Action::Help)])
	                : "?");
	std::fflush(stdout);
}

bool OnButton(int32_t controller, int sdl_button, bool down, uint32_t pad_button) {
	if (sdl_button < 0 || sdl_button >= SDL_CONTROLLER_BUTTON_MAX) return false;
	auto& pad                                 = g_pads[controller];
	pad.held[static_cast<size_t>(sdl_button)] = down;
	// The quit shortcut (QuitChord): all its buttons down starts its hold, any of them up ends it.
	if (!g_quit_chord.empty()) {
		const bool all = std::all_of(g_quit_chord.begin(), g_quit_chord.end(), [&](int button) {
			return pad.held[static_cast<size_t>(button)];
		});
		if (all && !pad.quit_armed) {
			pad.quit_armed = true;
			After(QuitChordMs(), QuitChordHeld, controller,
			      static_cast<int32_t>(++g_quit_chord_token));
		} else if (!all && pad.quit_armed) {
			pad.quit_armed = false;
			++g_quit_chord_token;
		}
	}
	// Choosing the modifier: the next button pressed.
	if (g_rebinding && down) {
		g_rebinding = false;
		// (Held now as the modifier: its release is no tap for the game.)
		pad.modifier_down = true;
		pad.chord_used    = true;
		SetModifier(sdl_button, true);
		Notice(std::string("Hotkey button: ") + DisplayName(sdl_button) + " (saved)");
		return true;
	}
	if (sdl_button == g_modifier) {
		if (down) {
			pad.modifier_down = true;
			pad.chord_used    = false;
			return true;
		}
		pad.modifier_down                              = false;
		pad.swallowed[static_cast<size_t>(sdl_button)] = false;
		++g_quit_token; // (a quit being held is let go)
		// Tapped alone, a modifier the game uses reaches it: pressed now, released a moment later.
		// (Only with the hotkeys' own events for the release: else the button would stay held.)
		if (!pad.chord_used && pad_button != 0 && g_event_type != static_cast<uint32_t>(-1)) {
			Libs::Controller::SetButton(controller, pad_button, true);
			After(80, TapRelease, controller, static_cast<int32_t>(pad_button));
		}
		return true;
	}
	if (down && pad.modifier_down) {
		pad.chord_used                                 = true;
		pad.swallowed[static_cast<size_t>(sdl_button)] = true;
		if (const int action = ActionFor(sdl_button); action >= 0)
			Run(static_cast<Action>(action), controller);
		return true;
	}
	if (!down && pad.swallowed[static_cast<size_t>(sdl_button)]) {
		pad.swallowed[static_cast<size_t>(sdl_button)] = false;
		if (sdl_button == g_bind[static_cast<size_t>(Action::Quit)]) ++g_quit_token;
		return true;
	}
	return false;
}

bool OnEvent(const SDL_Event& event) {
	if (g_event_type == static_cast<uint32_t>(-1) || event.type != g_event_type) return false;
	const auto a = static_cast<int32_t>(reinterpret_cast<intptr_t>(event.user.data1));
	const auto b = static_cast<int32_t>(reinterpret_cast<intptr_t>(event.user.data2));
	switch (event.user.code) {
		case QuitHeld:
			// Still held (the token unchanged): quit.
			if (static_cast<uint32_t>(b) == g_quit_token && a == g_quit_controller &&
			    g_pads[a].modifier_down) {
				Notice("Quitting...", 2);
				if (g_actions.quit != nullptr) g_actions.quit();
			}
			break;
		case QuitChordHeld:
			if (static_cast<uint32_t>(b) == g_quit_chord_token && g_pads[a].quit_armed) {
				Notice("Quitting...", 2);
				if (g_actions.quit != nullptr) g_actions.quit();
			}
			break;
		case TapRelease: Libs::Controller::SetButton(a, static_cast<uint32_t>(b), false); break;
		case RebindTimeout:
			if (g_rebinding && static_cast<uint32_t>(b) == g_rebind_token) {
				g_rebinding = false;
				Notice(std::string("Hotkey button kept: ") + DisplayName(g_modifier));
			}
			break;
		default: break;
	}
	return true;
}

std::string Describe() {
	std::string text = std::string("Hold ") + DisplayName(g_modifier) + " +";
	for (size_t i = 0; i < ActionCount; ++i) {
		if (g_bind[i] == SDL_CONTROLLER_BUTTON_INVALID || g_bind[i] == g_modifier) continue;
		text += std::string("\n  ") + DisplayName(g_bind[i]) + ": " + Infos[i].label;
	}
	if (!g_quit_chord.empty()) {
		text += "\nQuit: hold ";
		for (size_t i = 0; i < g_quit_chord.size(); ++i)
			text += std::string(i != 0 ? " + " : "") + DisplayName(g_quit_chord[i]);
		text += g_quit_chord.size() == 1 ? " for 2 s" : " together for 1 s";
	}
	return text;
}

} // namespace Local::ControllerHotkeys
