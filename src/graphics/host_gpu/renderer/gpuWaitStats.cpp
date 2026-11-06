#include "graphics/host_gpu/renderer/gpuWaitStats.h"

#include "common/diagLog.h"
#include "common/logging/log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Libs::Graphics::GpuWaitStats {
namespace {

thread_local const char* t_reason = nullptr;
thread_local uint64_t    t_exception_rva = 0;

struct Totals {
	uint64_t submits     = 0;
	uint64_t waits       = 0;
	uint64_t nanoseconds = 0;
	uint64_t events      = 0;
};

constexpr size_t MaxReadbackWaits = 32;
constexpr size_t MaxReadbackPages = 8;

struct ReadbackPage {
	uint64_t page        = 0;
	uint64_t bytes       = 0;
	uint64_t nanoseconds = 0;
	uint64_t count       = 0;
};

struct ReadbackWait {
	char                              reason[48] {};
	uint64_t                          rva         = 0;
	uint64_t                          nanoseconds = 0;
	uint64_t                          count       = 0;
	std::array<ReadbackPage, MaxReadbackPages> pages {};
};

struct State {
	std::mutex                                     mutex;
	std::unordered_map<std::string, Totals>        totals;
	std::chrono::steady_clock::time_point          next = std::chrono::steady_clock::now();
	std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> pages; // count, bytes
	uint64_t                                       gpu_busy_ns = 0;
	std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> shaders; // ns, count
	std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> pixel_shaders; // ns, count
	std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> cpu;          // ns, count
	std::array<ReadbackWait, MaxReadbackWaits>     readback_waits {};
};

// Nanoseconds this thread has spent in blocking GPU waits, so CPU timers can exclude them.
thread_local uint64_t t_wait_ns = 0;

uint64_t ModuleRelativeAddress(uint64_t instruction_address) noexcept {
	if (!Enabled() || instruction_address == 0) return 0;
#if defined(_WIN32)
	const auto base = reinterpret_cast<uint64_t>(GetModuleHandle(nullptr));
#else
	Dl_info module {};
	if (dladdr(reinterpret_cast<void*>(&ModuleRelativeAddress), &module) == 0 ||
	    module.dli_fbase == nullptr) {
		return 0;
	}
	const auto base = reinterpret_cast<uint64_t>(module.dli_fbase);
#endif
	return instruction_address >= base ? instruction_address - base : 0;
}

uint64_t NowNs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
	                                 std::chrono::steady_clock::now().time_since_epoch())
	                                 .count());
}

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
		if (a.second.nanoseconds != b.second.nanoseconds) {
			return a.second.nanoseconds > b.second.nanoseconds;
		}
		if (a.second.submits != b.second.submits) return a.second.submits > b.second.submits;
		return a.first < b.first;
	});
	std::string wait_top = "wait-top";
	for (size_t i = 0; i < rows.size() && i < 5u; ++i) {
		const auto& [reason, totals] = rows[i];
		char line[160];
		if (totals.events != 0 && totals.submits == 0 && totals.waits == 0) {
			std::snprintf(line, sizeof(line), " %s events=%llu;", reason.c_str(),
			              static_cast<unsigned long long>(totals.events));
		} else {
			std::snprintf(line, sizeof(line), " %s wait_ms=%.1f waits=%llu submits=%llu;",
			              reason.c_str(), static_cast<double>(totals.nanoseconds) / 1e6,
			              static_cast<unsigned long long>(totals.waits),
			              static_cast<unsigned long long>(totals.submits));
		}
		wait_top += line;
	}
	wait_top += "\n";
	WriteDiag(wait_top.c_str());
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
	if (!state.cpu.empty()) {
		std::vector<std::pair<std::string, std::pair<uint64_t, uint64_t>>> cpu(state.cpu.begin(),
		                                                                       state.cpu.end());
		state.cpu.clear();
		std::sort(cpu.begin(), cpu.end(),
		          [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
		text += "  CPU time excluding GPU waits (2 s):";
		for (size_t i = 0; i < cpu.size() && i < 32; i++) {
			char line[128];
			std::snprintf(line, sizeof(line), " %s %.1fms x%llu;", cpu[i].first.c_str(),
			              static_cast<double>(cpu[i].second.first) / 1e6,
			              static_cast<unsigned long long>(cpu[i].second.second));
			text += line;
		}
		text += "\n";
	}
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
	if (!state.shaders.empty() || !state.pixel_shaders.empty()) {
		struct Entry {
			uint64_t                       key;
			bool                           pixel;
			std::pair<uint64_t, uint64_t> second;
		};
		std::vector<Entry> shaders;
		for (const auto& [key, value]: state.shaders) shaders.push_back({key, false, value});
		for (const auto& [key, value]: state.pixel_shaders) shaders.push_back({key, true, value});
		state.shaders.clear();
		state.pixel_shaders.clear();
		std::sort(shaders.begin(), shaders.end(),
		          [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
		uint64_t total = 0;
		for (const auto& entry: shaders) total += entry.second.first;
		char head[64];
		std::snprintf(head, sizeof(head), "  GPU time by shader (sum %.0f ms):",
		              static_cast<double>(total) / 1e6);
		text += head;
		for (size_t i = 0; i < shaders.size() && i < 12; i++) {
			const auto key = shaders[i].key;
			char       line[96];
			std::snprintf(line, sizeof(line), " %s%016llx %.1fms x%llu;",
			              shaders[i].pixel ? "ps:" : "cs:", static_cast<unsigned long long>(key),
			              static_cast<double>(shaders[i].second.first) / 1e6,
			              static_cast<unsigned long long>(shaders[i].second.second));
			text += line;
		}
		text += "\n";
	}
	if (std::any_of(state.readback_waits.begin(), state.readback_waits.end(),
	                [](const auto& entry) { return entry.count != 0; })) {
		std::vector<ReadbackWait> readbacks;
		for (const auto& entry: state.readback_waits) {
			if (entry.count != 0) readbacks.push_back(entry);
		}
		state.readback_waits = {};
		std::sort(readbacks.begin(), readbacks.end(), [](const auto& a, const auto& b) {
			if (a.nanoseconds != b.nanoseconds) return a.nanoseconds > b.nanoseconds;
			const int reason_order = std::strcmp(a.reason, b.reason);
			return reason_order != 0 ? reason_order < 0 : a.rva < b.rva;
		});
		std::string readback_text = "gpu-thread-readback top=";
		for (size_t i = 0; i < readbacks.size() && i < 8; ++i) {
			const auto& entry = readbacks[i];
			char        rva[24];
			if (entry.rva != 0) {
				std::snprintf(rva, sizeof(rva), "0x%llx",
				              static_cast<unsigned long long>(entry.rva));
			} else {
				std::snprintf(rva, sizeof(rva), "none");
			}
			char head[192];
			std::snprintf(head, sizeof(head), " %s rva=%s wait_ms=%.1f waits=%llu pages=", entry.reason,
			              rva, static_cast<double>(entry.nanoseconds) / 1e6,
			              static_cast<unsigned long long>(entry.count));
			readback_text += head;
			std::vector<ReadbackPage> pages;
			for (const auto& page: entry.pages) {
				if (page.count != 0) pages.push_back(page);
			}
			std::sort(pages.begin(), pages.end(), [](const auto& a, const auto& b) {
				return a.nanoseconds > b.nanoseconds;
			});
			for (size_t p = 0; p < pages.size(); ++p) {
				char page[80];
				std::snprintf(page, sizeof(page), "%s0x%llx+0x%llx(x%llu)", p != 0 ? "," : "",
				              static_cast<unsigned long long>(pages[p].page),
				              static_cast<unsigned long long>(pages[p].bytes),
				              static_cast<unsigned long long>(pages[p].count));
				readback_text += page;
			}
			readback_text += ";";
		}
		readback_text += "\n";
		WriteDiag(readback_text.c_str());
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

bool ProfileEnabled() noexcept {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_GPU_PROFILE");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

bool WaitStatsEnabled() noexcept {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_WAIT_STATS");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

bool Enabled() noexcept {
	return WaitStatsEnabled() || ProfileEnabled();
}

void RecordShaderTime(uint64_t key, bool pixel, uint64_t nanoseconds) noexcept {
	if (!ProfileEnabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	auto&            entry = pixel ? state.pixel_shaders[key] : state.shaders[key];
	entry.first += nanoseconds;
	entry.second++;
}

void RecordSubmit() noexcept {
	if (Enabled()) Record(1, 0, 0);
}

void RecordWait(uint64_t nanoseconds) noexcept {
	t_wait_ns += nanoseconds;
	if (Enabled()) Record(0, 1, nanoseconds);
}

void RecordCpuTime(const char* name, uint64_t nanoseconds) noexcept {
	if (!Enabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	auto&            entry = state.cpu[name];
	entry.first += nanoseconds;
	entry.second++;
}

CpuTimer::CpuTimer(const char* name) noexcept: m_name(name), m_enabled(Enabled()) {
	if (m_enabled) {
		m_start      = NowNs();
		m_wait_start = t_wait_ns;
	}
}

CpuTimer::~CpuTimer() {
	if (!m_enabled) return;
	const auto elapsed = NowNs() - m_start;
	const auto waited  = t_wait_ns - m_wait_start;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	auto&            entry = state.cpu[m_name];
	entry.first += elapsed > waited ? elapsed - waited : 0;
	entry.second++;
}

void RecordFaultPage(uint64_t page, uint64_t bytes) noexcept {
	if (!Enabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	auto&            entry = state.pages[page];
	entry.first++;
	entry.second += bytes;
}

void RecordGpuThreadReadbackWait(uint64_t page, uint64_t bytes, uint64_t nanoseconds) noexcept {
	if (!WaitStatsEnabled()) return;
	auto&            state = GetState();
	std::scoped_lock lock {state.mutex};
	const char*      reason = CurrentReason() != nullptr ? CurrentReason() : "none";
	auto             entry  = std::find_if(state.readback_waits.begin(), state.readback_waits.end(),
	                                      [&](const auto& candidate) {
		                                      return candidate.count != 0 && candidate.rva == t_exception_rva &&
		                                             std::strcmp(candidate.reason, reason) == 0;
	                                      });
	if (entry == state.readback_waits.end()) {
		entry = std::find_if(state.readback_waits.begin(), state.readback_waits.end(),
		                     [](const auto& candidate) { return candidate.count == 0; });
	}
	if (entry == state.readback_waits.end()) {
		entry = std::min_element(state.readback_waits.begin(), state.readback_waits.end(),
		                         [](const auto& a, const auto& b) {
			                         return a.nanoseconds < b.nanoseconds;
		                         });
	}
	if (entry->rva != t_exception_rva || entry->count == 0 ||
	    std::strcmp(entry->reason, reason) != 0) {
		*entry = ReadbackWait {};
		std::snprintf(entry->reason, sizeof(entry->reason), "%s", reason);
		entry->rva = t_exception_rva;
	}
	entry->nanoseconds += nanoseconds;
	entry->count++;
	auto page_entry = std::find_if(entry->pages.begin(), entry->pages.end(), [&](const auto& candidate) {
		return candidate.count != 0 && candidate.page == page && candidate.bytes == bytes;
	});
	if (page_entry == entry->pages.end()) {
		page_entry = std::find_if(entry->pages.begin(), entry->pages.end(),
		                          [](const auto& candidate) { return candidate.count == 0; });
	}
	if (page_entry == entry->pages.end()) {
		page_entry = std::min_element(entry->pages.begin(), entry->pages.end(),
		                              [](const auto& a, const auto& b) {
			                              return a.nanoseconds < b.nanoseconds;
		                              });
	}
	if (page_entry->count == 0 || page_entry->page != page || page_entry->bytes != bytes) {
		*page_entry = ReadbackPage {.page = page, .bytes = bytes};
	}
	page_entry->nanoseconds += nanoseconds;
	page_entry->count++;
	MaybePrint(state);
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

void WriteDiag(const char* text) noexcept {
	if (!Enabled()) return;
	Common::DiagLog::Write(text);
}

const char* CurrentReason() noexcept {
	return t_reason;
}

ExceptionScope::ExceptionScope(uint64_t exception_address) noexcept: m_previous(t_exception_rva) {
	t_exception_rva = ModuleRelativeAddress(exception_address);
}

ExceptionScope::~ExceptionScope() {
	t_exception_rva = m_previous;
}

Scope::Scope(const char* reason) noexcept: m_previous(t_reason) {
	t_reason = reason;
}

Scope::~Scope() {
	t_reason = m_previous;
}

} // namespace Libs::Graphics::GpuWaitStats
