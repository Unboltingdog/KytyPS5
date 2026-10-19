#include "local/haptics-rumble.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Libs::Controller {

namespace {

constexpr float LOW_BAND_HZ = 100.0f; // below: the large motor
constexpr float RELEASE_S   = 0.08f;  // how fast a level falls once the samples do
constexpr float LOW_SCALE   = 3.0f;   // RMS to strength (a full-scale sine is 0.71)
constexpr float HIGH_SCALE  = 4.0f;
constexpr float GATE        = 0.03f; // below: off (noise, a fading tail)
constexpr float TWO_PI      = 6.2831853f;

uint8_t Level(float envelope, float scale, float strength) {
	const float value = std::min(1.0f, envelope * scale * strength);
	if (value < GATE) {
		return 0;
	}
	// sqrt: a motor's felt strength rises slowly at the bottom of its range.
	return static_cast<uint8_t>(std::lround(255.0f * std::sqrt(value)));
}

} // namespace

RumbleLevel HapticsToRumble::Feed(const void* data, uint32_t frames, uint32_t rate,
                                  uint32_t channels, bool is_float, const float* gains) {
	if (data == nullptr || frames == 0 || rate == 0 || channels == 0) {
		return {};
	}
	const uint32_t used  = std::min(channels, MAX_CHANNELS);
	const float    alpha = 1.0f - std::exp(-TWO_PI * LOW_BAND_HZ / static_cast<float>(rate));

	// Each channel's mean square in both bands; the strongest channel counts (the actuators are
	// left and right).
	float low_sum[MAX_CHANNELS] {}, high_sum[MAX_CHANNELS] {};
	for (uint32_t frame = 0; frame < frames; frame++) {
		for (uint32_t ch = 0; ch < used; ch++) {
			const size_t index = static_cast<size_t>(frame) * channels + ch;
			float        value =
			    is_float ? static_cast<const float*>(data)[index]
			             : static_cast<float>(static_cast<const int16_t*>(data)[index]) / 32768.0f;
			if (gains != nullptr) {
				value *= gains[ch];
			}
			m_low_pass[ch] += alpha * (value - m_low_pass[ch]);
			const float high = value - m_low_pass[ch];
			low_sum[ch] += m_low_pass[ch] * m_low_pass[ch];
			high_sum[ch] += high * high;
		}
	}
	float low = 0.0f, high = 0.0f;
	for (uint32_t ch = 0; ch < used; ch++) {
		low  = std::max(low, low_sum[ch]);
		high = std::max(high, high_sum[ch]);
	}
	low  = std::sqrt(low / static_cast<float>(frames));
	high = std::sqrt(high / static_cast<float>(frames));

	// Rises at once, falls over RELEASE_S.
	const float decay =
	    std::exp(-static_cast<float>(frames) / static_cast<float>(rate) / RELEASE_S);
	m_env_low  = std::max(low, m_env_low * decay);
	m_env_high = std::max(high, m_env_high * decay);

	return {Level(m_env_low, LOW_SCALE, m_strength), Level(m_env_high, HIGH_SCALE, m_strength)};
}

void HapticsToRumble::Reset() {
	for (auto& value: m_low_pass) {
		value = 0.0f;
	}
	m_env_low = m_env_high = 0.0f;
}

float HapticsRumbleStrength() {
	static const float strength = [] {
		const char* text = std::getenv("KYTY_HAPTICS_RUMBLE"); // NOLINT(concurrency-mt-unsafe)
		if (text == nullptr || *text == '\0') {
			return 1.0f;
		}
		char*       end   = nullptr;
		const float value = std::strtof(text, &end);
		return end == text || !(value >= 0.0f) ? 1.0f : std::min(value, 4.0f);
	}();
	return strength;
}

} // namespace Libs::Controller
