// Controller hotkeys (src/local/controller-hotkeys.h): chords taken from the game, the modifier changed and saved, a
// modifier the game uses tapped through, hold to quit, hotkeys.ini read and written. The emulator calls the hotkeys
// make are stubbed below; SDL's timers and events are real.
#include "SDL.h"
#include "controller-hotkeys.h"
#include "frame-gen.h"
#include "fsr.h"
#include "libs/controller.h"
#include "loader/demonsSoulsWarp.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// ---- Stubs of what the hotkeys call ----
namespace {
int         g_quits = 0, g_fullscreens = 0, g_pauses = 0, g_overlays = 0;
bool        g_frame_gen_on = true;
float       g_sharpness    = 0.25f;
int         g_button_presses = 0, g_button_releases = 0;
uint32_t    g_last_button  = 0;
std::string g_last_notice;
} // namespace

namespace Libs::Graphics::FrameGen {
const char* BackendName() {
	return "FSR 3";
}
bool UserEnabled() {
	return g_frame_gen_on;
}
void SetUserEnabled(bool on) {
	g_frame_gen_on = on;
}
namespace Fsr {
bool UpscalerReady() {
	return true;
}
float Sharpness() {
	return g_sharpness;
}
void SetSharpness(float value) {
	g_sharpness = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}
} // namespace Fsr
} // namespace Libs::Graphics::FrameGen
namespace Libs::Controller {
void SetButton(int /*id*/, uint32_t button, bool down) {
	g_last_button = button;
	(down ? g_button_presses : g_button_releases)++;
}
} // namespace Libs::Controller
namespace Loader::DemonsSoulsWarp {
void ShowNotice(std::string message, int /*seconds*/) {
	g_last_notice = std::move(message);
}
} // namespace Loader::DemonsSoulsWarp

namespace {

namespace Hotkeys = Local::ControllerHotkeys;
constexpr int32_t Pad = 7;

void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "ControllerHotkeysTests: failed: %s\n", message);
		std::abort();
	}
}

std::string ReadFile(const std::filesystem::path& path) {
	std::ifstream     in(path);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

// The hotkeys' timer events, for `ms`.
void Pump(uint32_t ms) {
	const uint64_t until = SDL_GetTicks64() + ms;
	SDL_Event      event {};
	while (SDL_GetTicks64() < until) {
		if (SDL_WaitEventTimeout(&event, 10) != 0) (void)Hotkeys::OnEvent(event);
	}
}

bool Press(int button, uint32_t pad_button = 0) {
	return Hotkeys::OnButton(Pad, button, true, pad_button);
}
bool Release(int button, uint32_t pad_button = 0) {
	return Hotkeys::OnButton(Pad, button, false, pad_button);
}

void TestDefaultsWritten(const std::filesystem::path& file) {
	const auto text = ReadFile(file);
	Check(text.find("Modifier=Create") != std::string::npos, "the defaults' modifier is not Create");
	Check(text.find("Quit=Options") != std::string::npos, "the defaults' quit is not Options");
	Check(Hotkeys::Describe().find("Hold Create") == 0, "Describe does not start with the modifier");
}

void TestChordsAreTaken() {
	Check(!Press(SDL_CONTROLLER_BUTTON_Y), "Triangle alone did not reach the game");
	Check(!Release(SDL_CONTROLLER_BUTTON_Y), "Triangle's release alone did not reach the game");
	Check(Press(SDL_CONTROLLER_BUTTON_BACK), "the modifier reached the game");
	Check(Press(SDL_CONTROLLER_BUTTON_Y), "Triangle in a chord reached the game");
	Check(g_fullscreens == 1, "modifier + Triangle did not toggle full screen");
	Check(Release(SDL_CONTROLLER_BUTTON_BACK), "the modifier's release reached the game");
	// (Its press was taken: so is its release, the modifier already up.)
	Check(Release(SDL_CONTROLLER_BUTTON_Y), "a chord button's release reached the game");
	Check(Press(SDL_CONTROLLER_BUTTON_BACK) && Press(SDL_CONTROLLER_BUTTON_X) && Release(SDL_CONTROLLER_BUTTON_X) &&
	          Press(SDL_CONTROLLER_BUTTON_B) && Release(SDL_CONTROLLER_BUTTON_B) && Release(SDL_CONTROLLER_BUTTON_BACK),
	      "chords were not taken");
	Check(g_pauses == 1 && g_overlays == 1, "pause or overlay did not run");
	Check(g_button_presses == 0, "the unmapped modifier was tapped through to the game");
}

void TestFrameGenAndSharpness() {
	Press(SDL_CONTROLLER_BUTTON_BACK);
	Press(SDL_CONTROLLER_BUTTON_A);
	Release(SDL_CONTROLLER_BUTTON_A);
	Check(!g_frame_gen_on && g_last_notice.find("off") != std::string::npos, "Cross did not turn frame generation off");
	Press(SDL_CONTROLLER_BUTTON_A);
	Release(SDL_CONTROLLER_BUTTON_A);
	Check(g_frame_gen_on, "Cross did not turn frame generation back on");
	Press(SDL_CONTROLLER_BUTTON_DPAD_UP);
	Release(SDL_CONTROLLER_BUTTON_DPAD_UP);
	Check(g_sharpness > 0.34f && g_sharpness < 0.36f, "Up did not raise the sharpness by 0.1");
	for (int i = 0; i < 6; ++i) {
		Press(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
		Release(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
	}
	Check(g_sharpness == 0.0f, "the sharpness went below 0");
	Release(SDL_CONTROLLER_BUTTON_BACK);
}

void TestHoldToQuit() {
	// Let go early: no quit.
	Press(SDL_CONTROLLER_BUTTON_BACK);
	Press(SDL_CONTROLLER_BUTTON_START);
	Pump(300);
	Release(SDL_CONTROLLER_BUTTON_START);
	Pump(1000);
	Check(g_quits == 0, "a quit let go of early still quit");
	// The modifier let go early: no quit either.
	Press(SDL_CONTROLLER_BUTTON_START);
	Pump(300);
	Release(SDL_CONTROLLER_BUTTON_BACK);
	Pump(1000);
	Release(SDL_CONTROLLER_BUTTON_START);
	Check(g_quits == 0, "a quit whose modifier was let go of still quit");
	// Held a second: quit.
	Press(SDL_CONTROLLER_BUTTON_BACK);
	Press(SDL_CONTROLLER_BUTTON_START);
	Pump(1300);
	Check(g_quits == 1, "holding modifier + Options for a second did not quit");
	Release(SDL_CONTROLLER_BUTTON_START);
	Release(SDL_CONTROLLER_BUTTON_BACK);
}

void TestRebindAndTap(const std::filesystem::path& file) {
	// Modifier + R3, then L1: L1 is the modifier, saved.
	Press(SDL_CONTROLLER_BUTTON_BACK);
	Press(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
	Release(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
	Release(SDL_CONTROLLER_BUTTON_BACK);
	constexpr uint32_t L1 = Libs::Controller::PAD_BUTTON_L1;
	Check(Press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, L1), "the new modifier's press reached the game");
	Check(Release(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, L1), "the new modifier's release reached the game");
	Check(ReadFile(file).find("Modifier=L1") != std::string::npos, "the new modifier was not saved");
	Check(!Press(SDL_CONTROLLER_BUTTON_BACK), "Create is still a modifier");
	Check(!Release(SDL_CONTROLLER_BUTTON_BACK), "Create's release did not reach the game");
	// L1 tapped alone reaches the game (it uses L1): pressed at its release, released a moment later.
	const int presses = g_button_presses, releases = g_button_releases;
	Check(Press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, L1), "the modifier's press reached the game");
	Check(Release(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, L1), "the modifier's release reached the game");
	Check(g_button_presses == presses + 1 && g_last_button == L1, "a tapped L1 did not reach the game");
	Pump(200);
	Check(g_button_releases == releases + 1, "a tapped L1 was not released");
	// In a chord it does not.
	Press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, L1);
	Press(SDL_CONTROLLER_BUTTON_Y);
	Release(SDL_CONTROLLER_BUTTON_Y);
	Release(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, L1);
	Check(g_button_presses == presses + 1, "L1 used in a chord reached the game");
	Check(g_fullscreens == 2, "L1 + Triangle did not toggle full screen");
}

void TestFileParsing(const std::filesystem::path& file) {
	// A byte order mark, comments, PlayStation and SDL names, an unbound action, a wrong name.
	{
		std::ofstream out(file, std::ios::trunc);
		out << "\xEF\xBB\xBF" "Modifier = PS   # the PS button\n"
		    << "Quit=start\nFullscreen=\nPause=Touchpad\nOverlay=bogus\n";
	}
	Hotkeys::Actions none;
	Hotkeys::Initialize(none);
	const auto text = Hotkeys::Describe();
	Check(text.find("Hold PS") == 0, "the modifier after a byte order mark was not read");
	Check(text.find("Options: quit") != std::string::npos, "an SDL name was not read");
	Check(text.find("Touchpad: pause") != std::string::npos, "a PlayStation name was not read");
	Check(text.find("full screen") == std::string::npos, "an unbound action is still bound");
	Check(text.find("Circle: performance overlay") != std::string::npos, "a wrong name did not keep the default");
}

} // namespace

int main() {
	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
		std::fprintf(stderr, "ControllerHotkeysTests: SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	const auto dir  = std::filesystem::temp_directory_path() / "kyty-hotkeys-tests";
	const auto file = dir / "hotkeys.ini";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
#if defined(_WIN32)
	_putenv_s("KYTY_HOTKEYS_FILE", file.string().c_str());
#else
	setenv("KYTY_HOTKEYS_FILE", file.string().c_str(), 1);
#endif
	Hotkeys::Actions actions;
	actions.quit              = [] { ++g_quits; };
	actions.toggle_fullscreen = [] { ++g_fullscreens; };
	actions.toggle_pause      = [] { ++g_pauses; };
	actions.cycle_overlay     = [] { ++g_overlays; };
	Hotkeys::Initialize(actions);
	TestDefaultsWritten(file);
	TestChordsAreTaken();
	TestFrameGenAndSharpness();
	TestHoldToQuit();
	TestRebindAndTap(file);
	TestFileParsing(file);
	std::filesystem::remove_all(dir);
	SDL_Quit();
	std::printf("ControllerHotkeysTests: passed\n");
	return 0;
}
