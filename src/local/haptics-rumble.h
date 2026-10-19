#ifndef EMULATOR_SRC_LOCAL_HAPTICS_RUMBLE_H_
#define EMULATOR_SRC_LOCAL_HAPTICS_RUMBLE_H_
// A DualSense's haptics as rumble for other controllers. PS5 games such as Demon's Souls vibrate
// the pad through an audio port (AUDIO_OUT_PORT_TYPE_VIBRATION) whose samples drive the DualSense's
// actuators, not through scePadSetVibration; without a DualSense on USB (libs/dualSenseHaptics.h)
// those samples went nowhere and an Xbox-style pad, a Legion Go's or a DualSense over Bluetooth
// never vibrated. This turns the samples' envelope into the two motors: the low band (the heavy
// hits) into the large motor, the rest (the buzz, the ticks) into the small one.
//   KYTY_HAPTICS_RUMBLE=<float>   strength, 0 off (default 1; the launcher's "Vibration" sets it)
#include <cstdint>

namespace Libs::Controller {

struct RumbleLevel {
	uint8_t large = 0, small = 0;
};

class HapticsToRumble {
public:
	static constexpr uint32_t MAX_CHANNELS = 8;

	void SetStrength(float strength) { m_strength = strength < 0.0f ? 0.0f : strength; }
	// One buffer of the vibration port: `frames` frames of `channels` channels, signed 16-bit or
	// float, scaled by `gains` (one per channel) when not null. Returns the motors' levels after
	// it.
	RumbleLevel Feed(const void* data, uint32_t frames, uint32_t rate, uint32_t channels,
	                 bool is_float, const float* gains);
	void        Reset();
	// The last buffer's largest sample (after `gains`), 0..1: the log's check that the game sends
	// anything.
	[[nodiscard]] float Peak() const { return m_peak; }

private:
	float m_peak = 0.0f;
	float m_low_pass[MAX_CHANNELS] {};
	float m_env_low = 0.0f, m_env_high = 0.0f;
	float m_strength = 1.0f;
};

// KYTY_HAPTICS_RUMBLE, read once: 0 when off.
float HapticsRumbleStrength();

} // namespace Libs::Controller

#endif // EMULATOR_SRC_LOCAL_HAPTICS_RUMBLE_H_
