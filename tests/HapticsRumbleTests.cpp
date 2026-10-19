// DualSense haptics as rumble (src/local/haptics-rumble.h): silence stays off, a low tone drives
// the large motor, a higher one the small motor, the level falls once the samples stop, 16-bit and
// float agree, and strength 0 is off.
#include "local/haptics-rumble.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using Libs::Controller::HapticsToRumble;
using Libs::Controller::RumbleLevel;

constexpr uint32_t RATE = 48000, FRAMES = 256, CHANNELS = 2;

void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "HapticsRumbleTests: failed: %s\n", message);
		std::abort();
	}
}

// `buffers` buffers of a sine at `hz` and `amplitude` on both channels (0 Hz: silence); the last
// level.
RumbleLevel Play(HapticsToRumble& rumble, float hz, float amplitude, int buffers,
                 bool as_float = true) {
	RumbleLevel          level;
	std::vector<float>   samples(FRAMES * CHANNELS);
	std::vector<int16_t> shorts(FRAMES * CHANNELS);
	uint64_t t = 0; // (each call from phase 0: the two formats compare sample for sample)
	for (int b = 0; b < buffers; b++) {
		for (uint32_t i = 0; i < FRAMES; i++, t++) {
			const float value =
			    amplitude * std::sin(6.2831853f * hz * static_cast<float>(t) / RATE);
			for (uint32_t ch = 0; ch < CHANNELS; ch++) {
				samples[i * CHANNELS + ch] = value;
				shorts[i * CHANNELS + ch]  = static_cast<int16_t>(value * 32767.0f);
			}
		}
		level = as_float ? rumble.Feed(samples.data(), FRAMES, RATE, CHANNELS, true, nullptr)
		                 : rumble.Feed(shorts.data(), FRAMES, RATE, CHANNELS, false, nullptr);
	}
	return level;
}

void TestSilence() {
	HapticsToRumble rumble;
	const auto      level = Play(rumble, 0.0f, 0.0f, 20);
	Check(level.large == 0 && level.small == 0, "silence vibrates");
	Check(rumble.Feed(nullptr, FRAMES, RATE, CHANNELS, true, nullptr).large == 0,
	      "no data vibrates");
}

void TestBands() {
	HapticsToRumble low;
	const auto      thump = Play(low, 40.0f, 0.5f, 20);
	Check(thump.large > 150, "a strong 40 Hz tone does not drive the large motor");
	Check(thump.large > thump.small, "a 40 Hz tone drives the small motor more than the large");
	HapticsToRumble high;
	const auto      buzz = Play(high, 320.0f, 0.5f, 20);
	Check(buzz.small > 150, "a strong 320 Hz tone does not drive the small motor");
	Check(buzz.small > buzz.large, "a 320 Hz tone drives the large motor more than the small");
	HapticsToRumble faint;
	const auto      hum = Play(faint, 40.0f, 0.005f, 20);
	Check(hum.large == 0 && hum.small == 0, "a faint hum vibrates");
}

void TestRelease() {
	HapticsToRumble rumble;
	Play(rumble, 40.0f, 0.5f, 20);
	const auto soon = Play(rumble, 0.0f, 0.0f, 2); // ~11 ms later
	Check(soon.large > 0, "the level drops at once");
	const auto later = Play(rumble, 0.0f, 0.0f, 100); // ~0.5 s later
	Check(later.large == 0 && later.small == 0, "the level does not fall to zero");
}

void TestFormats() {
	HapticsToRumble as_float, as_short;
	const auto      a = Play(as_float, 60.0f, 0.4f, 20, true);
	const auto      b = Play(as_short, 60.0f, 0.4f, 20, false);
	Check(std::abs(a.large - b.large) <= 4 && std::abs(a.small - b.small) <= 4,
	      "16-bit and float differ");
}

void TestStrength() {
	HapticsToRumble off;
	off.SetStrength(0.0f);
	const auto none = Play(off, 40.0f, 0.5f, 20);
	Check(none.large == 0 && none.small == 0, "strength 0 vibrates");
	HapticsToRumble weak, strong;
	weak.SetStrength(0.5f);
	strong.SetStrength(2.0f);
	Check(Play(weak, 40.0f, 0.1f, 20).large < Play(strong, 40.0f, 0.1f, 20).large,
	      "strength does not scale");
	const float        gains[CHANNELS] = {0.0f, 0.0f};
	std::vector<float> loud(FRAMES * CHANNELS, 0.9f);
	HapticsToRumble    muted;
	Check(muted.Feed(loud.data(), FRAMES, RATE, CHANNELS, true, gains).large == 0,
	      "the guest's zero gains vibrate");
}

} // namespace

int main() {
	TestSilence();
	TestBands();
	TestRelease();
	TestFormats();
	TestStrength();
	std::printf("HapticsRumbleTests: passed\n");
	return 0;
}
