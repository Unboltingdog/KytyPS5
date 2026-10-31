#include "graphics/host_gpu/renderer/gpuWaitStats.h"

#include "common/logging/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Libs::Graphics::GpuWaitStats {
namespace {

thread_local const char* t_reason = nullptr;

struct Totals {
	uint64_t submits     = 0;
	uint64_t waits       = 0;
	uint64_t nanoseconds = 0;
	uint64_t events      = 0;
};

struct State {
	std::mutex                                     mutex;
	std::unordered_map<std::string, Totals>        totals;
	std::chrono::steady_clock::time_point          next = std::chrono::steady_clock::now();
	std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> pages; // count, bytes
	uint64_t                                       gpu_busy_ns = 0;
};

State& GetState() {
	static State state;
	return state;
}

// Called with the mutex held.
void MaybePrint(State& state) {
	const auto now = std::chrono::steady_clock::now();
	if (now < state.next) return;
	state.next = now + std::chrono::seconds(2);
	std::vector<std::pair<std::string, Totals>> rows(state.totals.begin(), state.totals.end());
	state.totals.clear();
	std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
		return a.second.nanoseconds + a.second.submits > b.second.nanoseconds + b.second.submits;
	});
	char busy[64];
	std::snprintf(busy, sizeof(busy), " gpu_busy_ms=%.0f;",
	              static_cast<double>(state.gpu_busy_ns) / 1e6);
	state.gpu_busy_ns = 0;
	std::string text = std::string("GPU waits (2 s):") + busy;
	for (const auto& [reason, totals]: rows) {
		char line[160];
		if (totals.events != 0 && totals.submits == 0 && totals.waits == 0) {
			std::snprintf(line, sizeof(line), " %s events=%llu;", reason.c_str(),
			              static_cast<unsigned long long>(totals.events));
		} else {
			std::snprintf(line, sizeof(line), " %s submits=%llu waits=%llu wait_ms=%.0f;",
			              reason.c_str(), static_cast<unsigned long long>(totals.submits),
			              static_cast<unsigned long long>(totals.waits),
			              static_cast<double>(totals.nanoseconds) / 1e6);
		}
		text += line;
	}
	text += "\n";
	if (!state.pages.empty()) {
		std::vector<std::pair<uint64_t, std::pair<uint64_t, uint64_t>>> pages(state.pages.begin(),
		                                                                     state.pages.end());
		state.pages.clear();
		std::sort(pages.begin(), pages.end(),
		          [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
		text += "  busiest readback pages:";
		for (size_t i = 0; i < pages.size() && i < 8; i++) {
			char line[96];
			std::snprintf(line, sizeof(line), " 0x%llx%s x%llu (%llu KB)",
			              static_cast<unsigned long long>(pages[i].first & ~uint64_t {1}),
			              (pages[i].first & 1u) != 0 ? "w" : "r",
			              static_cast<unsigned long long>(pages[i].second.first),
			              static_cast<unsigned long long>(pages[i].second.second /
			                                              std::max<uint64_t>(pages[i].second.first, 1) /
			                                              1024));
			text += line;
		}
		char tail[64];
		std::snprintf(tail, sizeof(tail), " (%zu pages)\n", pages.size());
		text += tail;
	}
	Log::WriteToConsoleAndLog(text);
}

void Record(uint64_t submits, uint64_t waits, uint64_t nanoseconds) noexcept {
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	auto&            totals = state.totals[t_reason != nullptr ? t_reason : "other"];
	totals.submits += submits;
	totals.waits += waits;
	totals.nanoseconds += nanoseconds;
	MaybePrint(state);
}

} // namespace

bool Enabled() noexcept {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_WAIT_STATS");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

void RecordSubmit() noexcept {
	if (Enabled()) Record(1, 0, 0);
}

void RecordWait(uint64_t nanoseconds) noexcept {
	if (Enabled()) Record(0, 1, nanoseconds);
}

void RecordFaultPage(uint64_t page, uint64_t bytes) noexcept {
	if (!Enabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	auto&            entry = state.pages[page];
	entry.first++;
	entry.second += bytes;
}

void RecordGpuBusy(uint64_t nanoseconds) noexcept {
	if (!Enabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	state.gpu_busy_ns += nanoseconds;
}

void RecordEvent(const char* name, uint64_t count) noexcept {
	if (!Enabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	state.totals[name].events += count;
	MaybePrint(state);
}

const char* CurrentReason() noexcept {
	return t_reason;
}

Scope::Scope(const char* reason) noexcept: m_previous(t_reason) {
	t_reason = reason;
}

Scope::~Scope() {
	t_reason = m_previous;
}

} // namespace Libs::Graphics::GpuWaitStats
