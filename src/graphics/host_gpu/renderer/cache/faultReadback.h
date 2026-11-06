#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTREADBACK_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTREADBACK_H_

#include "graphics/host_gpu/renderer/gpuWaitStats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

namespace Libs::Graphics::FaultReadback {

enum class WaitMode {
	Synchronous,
	OffGpu,
};

[[nodiscard]] inline bool OffGpuEnabled() {
	const char* value = std::getenv("KYTY_FAULT_WAIT_OFF_GPU");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

[[nodiscard]] inline WaitMode GetWaitMode(bool cpu_fault, bool gpu_thread, bool deferred) {
	return OffGpuEnabled() && cpu_fault && !gpu_thread && !deferred ? WaitMode::OffGpu
	                                                               : WaitMode::Synchronous;
}

struct BeginResult {
	std::vector<std::shared_ptr<std::atomic_bool>> waits;
	uint64_t                                       wait_tick      = 0;
	bool                                           found_readback = false;
};

template <typename Readbacks, typename Scheduler>
[[nodiscard]] BeginResult Begin(const Readbacks& readbacks, uint64_t vaddr, uint64_t size,
                                WaitMode mode, Scheduler& scheduler) {
	BeginResult result;
	for (const auto& readback: readbacks) {
		if (readback.begin >= vaddr + size || vaddr >= readback.end) {
			continue;
		}
		result.found_readback = true;
		if (mode == WaitMode::OffGpu) {
			if (!readback.published->load(std::memory_order_acquire)) {
				result.waits.push_back(readback.published);
			}
		} else {
			result.wait_tick = std::max(result.wait_tick, readback.tick);
		}
	}
	if (mode == WaitMode::OffGpu) {
		if (!result.waits.empty()) {
			scheduler.Flush();
		}
	} else if (result.wait_tick != 0) {
		scheduler.Wait(result.wait_tick);
		scheduler.WaitPriorityOperations(result.wait_tick);
	}
	return result;
}

inline void Wait(std::span<const std::shared_ptr<std::atomic_bool>> waits) {
	if (waits.empty()) {
		return;
	}
	const auto begin = std::chrono::steady_clock::now();
	for (const auto& published: waits) {
		published->wait(false, std::memory_order_acquire);
	}
	GpuWaitStats::Scope reason("fault-readback-wait");
	GpuWaitStats::RecordWait(static_cast<uint64_t>(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
	        .count()));
}

} // namespace Libs::Graphics::FaultReadback

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTREADBACK_H_
