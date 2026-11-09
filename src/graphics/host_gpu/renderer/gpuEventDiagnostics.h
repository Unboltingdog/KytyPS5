#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPU_EVENT_DIAGNOSTICS_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPU_EVENT_DIAGNOSTICS_H_

#include "graphics/host_gpu/renderer/debugSample.h"
#include "kernel/eventQueue.h"
#include "loader/runtimeLinker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

namespace Libs::Graphics::GpuEventDiagnostics {

inline bool Enabled() {
	static const bool on = [] {
		const char* value = std::getenv("KYTY_DIAG_GPU_EVENTS");
		return value != nullptr && value[0] == '1';
	}();
	return on;
}

struct EventStats {
	std::map<LibKernel::EventQueue::KernelEqueue, std::string> registrations;
	uint64_t requests   = 0;
	uint64_t delivered  = 0;
	uint32_t first_frame = 0;
	uint32_t last_frame  = 0;
};

struct LatencyStats {
	std::deque<std::chrono::steady_clock::time_point> pending;
	uint64_t count   = 0;
	uint64_t total_ns = 0;
	uint64_t min_ns  = UINT64_MAX;
	uint64_t max_ns  = 0;
};

struct State {
	std::mutex mutex;
	std::map<int, EventStats> events;
	std::map<std::tuple<std::string, std::string, int>, uint32_t> packet_logs;
	LatencyStats cull_latency;
	std::chrono::steady_clock::time_point summary_at = std::chrono::steady_clock::now();
};

inline State& GetState() {
	static State state;
	return state;
}

inline std::string RegisteredBy(const EventStats& stats) {
	std::map<std::string, bool> unique_names;
	for (const auto& [eq, name]: stats.registrations) {
		unique_names[name] = true;
	}
	std::string result;
	for (const auto& [name, present]: unique_names) {
		if (!result.empty()) {
			result += ',';
		}
		result += name;
	}
	return result.empty() ? "none" : result;
}

inline void ReportLocked(State& state, std::chrono::steady_clock::time_point now) {
	if (now - state.summary_at < std::chrono::seconds(20)) {
		return;
	}
	state.summary_at = now;
	for (const auto& [id, stats]: state.events) {
		const auto registered_by = RegisteredBy(stats);
		std::fprintf(stderr,
		             "GPU_EVENT_SUM id=0x%x registered_by=%s requests=%llu delivered=%llu first_frame=%u last_frame=%u\n",
		             id, registered_by.c_str(), static_cast<unsigned long long>(stats.requests),
		             static_cast<unsigned long long>(stats.delivered), stats.first_frame, stats.last_frame);
		if (!stats.registrations.empty() && stats.delivered == 0) {
			std::fprintf(stderr,
			             "GPU_EVENT_SUM id=0x%x registered_by=%s requests=%llu delivered=0 first_frame=%u last_frame=%u zero_delivery=1\n",
			             id, registered_by.c_str(), static_cast<unsigned long long>(stats.requests),
			             stats.first_frame, stats.last_frame);
		}
	}
}

inline void Report() {
	if (!Enabled()) {
		return;
	}
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	ReportLocked(state, std::chrono::steady_clock::now());
}

inline bool FrameGapLatencyEnabled() {
	static const bool on = [] {
		const char* value = std::getenv("KYTY_DIAG_FRAME_GAPS");
		return value != nullptr && value[0] == '1';
	}();
	return on;
}

inline void NoteRequest(int event_id) {
	if (event_id != 0x52 || !FrameGapLatencyEnabled()) return;
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	auto& pending = state.cull_latency.pending;
	if (pending.size() >= 4096u) pending.pop_front();
	pending.push_back(std::chrono::steady_clock::now());
}

inline void NoteDelivery(int event_id, bool delivered) {
	if (event_id != 0x52 || !FrameGapLatencyEnabled()) return;
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	auto& stats = state.cull_latency;
	if (stats.pending.empty()) return;
	const auto requested = stats.pending.front();
	stats.pending.pop_front();
	if (!delivered) return;
	const auto ns = static_cast<uint64_t>(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - requested).count());
	stats.count++;
	stats.total_ns += ns;
	stats.min_ns = std::min(stats.min_ns, ns);
	stats.max_ns = std::max(stats.max_ns, ns);
}

inline void ReportFrameGaps() {
	if (!FrameGapLatencyEnabled()) return;
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	auto& stats = state.cull_latency;
	const auto ms = [](uint64_t ns) { return static_cast<double>(ns) / 1.0e6; };
	std::fprintf(stderr,
	             "FRAME_GAPS_EVENT id=0x52 name=ProxySet_Cull n=%llu min_ms=%.3f avg_ms=%.3f max_ms=%.3f pending=%zu\n",
	             static_cast<unsigned long long>(stats.count), stats.count != 0 ? ms(stats.min_ns) : 0.0,
	             stats.count != 0 ? ms(stats.total_ns) / stats.count : 0.0, ms(stats.max_ns), stats.pending.size());
	stats.count = 0;
	stats.total_ns = 0;
	stats.min_ns = UINT64_MAX;
	stats.max_ns = 0;
}

inline void Registration(const char* op, LibKernel::EventQueue::KernelEqueue eq, int id, void* udata,
                         uint64_t caller, bool registered) {
	if (!Enabled()) {
		return;
	}
	const auto name = LibKernel::EventQueue::KernelEqueueName(eq);
	const auto caller_text = caller != 0 ? ::Loader::DescribeGuestCaller(caller) : "unknown+0x0";
	std::fprintf(stderr, "GPU_EVENT_REG op=%s eq=%s@0x%llx id=0x%x udata=%p caller=%s\n", op,
	             name.c_str(), static_cast<unsigned long long>(eq), id, udata, caller_text.c_str());

	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	auto& stats = state.events[id];
	if (op[0] == 'a') {
		if (registered) {
			stats.registrations[eq] = name;
		}
	} else if (registered) {
		stats.registrations.erase(eq);
	}
	ReportLocked(state, std::chrono::steady_clock::now());
}

inline void Request(const char* queue, uint32_t queue_id, const char* packet, uint32_t interrupt_selector,
                    int event_id, bool delivered, const char* reason) {
	if (!Enabled()) {
		return;
	}
	const auto frame = DebugPresentCount().load(std::memory_order_relaxed);
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	auto& stats = state.events[event_id];
	stats.requests++;
	stats.delivered += delivered ? 1u : 0u;
	if (stats.requests == 1) {
		stats.first_frame = frame;
	}
	stats.last_frame = frame;
	const auto key = std::make_tuple(std::string(queue), std::string(packet), event_id);
	const auto count = ++state.packet_logs[key];
	if (count <= 50) {
		std::fprintf(stderr,
		             "GPU_EVENT_REQ queue=%s queue_id=0x%x packet=%s int_sel=0x%x event_id=0x%x delivered=%u reason=%s\n",
		             queue, queue_id, packet, interrupt_selector, event_id, delivered ? 1u : 0u,
		             delivered ? "-" : reason);
	}
	ReportLocked(state, std::chrono::steady_clock::now());
}

} // namespace Libs::Graphics::GpuEventDiagnostics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPU_EVENT_DIAGNOSTICS_H_
