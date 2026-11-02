#ifndef EMULATOR_INCLUDE_EMULATOR_LIBS_HLESTATS_H_
#define EMULATOR_INCLUDE_EMULATOR_LIBS_HLESTATS_H_

#include "common/logging/log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Libs::HleStats {

[[nodiscard]] inline bool Enabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_WAIT_STATS");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

class Timer {
public:
	explicit Timer(const char* name) noexcept: m_name(name), m_start(Enabled() ? NowNs() : 0u) {}
	~Timer() {
		if (m_start != 0u) Record(m_name, NowNs() - m_start);
	}
	Timer(const Timer&)            = delete;
	Timer& operator=(const Timer&) = delete;

private:
	struct State {
		std::mutex mutex;
		std::chrono::steady_clock::time_point next;
		std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> calls;
	};

	[[nodiscard]] static uint64_t NowNs() {
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		                                 std::chrono::steady_clock::now().time_since_epoch())
		                                 .count());
	}

	static void Record(const char* name, uint64_t nanoseconds) {
		auto& state = m_state;
		std::scoped_lock lock(state.mutex);
		auto& entry = state.calls[name];
		entry.first += nanoseconds;
		entry.second++;
		const auto now = std::chrono::steady_clock::now();
		if (now < state.next) return;
		state.next = now + std::chrono::seconds(2);
		std::vector<std::pair<std::string, std::pair<uint64_t, uint64_t>>> rows(
		    state.calls.begin(), state.calls.end());
		state.calls.clear();
		std::sort(rows.begin(), rows.end(),
		          [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
		std::string text = "HLE wall time (2 s):";
		for (size_t i = 0; i < rows.size() && i < 16u; ++i) {
			char line[160];
			std::snprintf(line, sizeof(line), " %s %.1fms x%llu;", rows[i].first.c_str(),
			              static_cast<double>(rows[i].second.first) / 1e6,
			              static_cast<unsigned long long>(rows[i].second.second));
			text += line;
		}
		text += '\n';
		Log::WriteToConsoleAndLog(text);
	}

	const char* m_name;
	uint64_t    m_start = 0;
	inline static State m_state;
};

} // namespace Libs::HleStats

#endif /* EMULATOR_INCLUDE_EMULATOR_LIBS_HLESTATS_H_ */
