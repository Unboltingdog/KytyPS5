#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUWAITSTATS_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUWAITSTATS_H_

#include <cstdint>

// KYTY_WAIT_STATS=1 (diagnostic, Unboltingdog fork): every two seconds, prints how many queue
// submissions and blocking GPU waits each part of the emulator caused, and how long the waits
// took. Code names its reason with a GpuWaitStats::Scope around the work that submits or waits.
namespace Libs::Graphics::GpuWaitStats {

[[nodiscard]] bool Enabled() noexcept;
void               RecordSubmit() noexcept;
void               RecordWait(uint64_t nanoseconds) noexcept;
// Adds GPU busy time measured with timestamps (shown as gpu_busy_ms).
void               RecordGpuBusy(uint64_t nanoseconds) noexcept;
// KYTY_GPU_PROFILE=1: adds one dispatch's or draw's GPU time to its shader (top ones listed).
[[nodiscard]] bool ProfileEnabled() noexcept;
void               RecordShaderTime(uint64_t key, bool pixel, uint64_t nanoseconds) noexcept;
// Counts one occurrence of a named event (shown as events=N).
void               RecordEvent(const char* name, uint64_t count = 1) noexcept;
// Counts a CPU readback fault at a page, with the size read back; the busiest pages are listed.
void               RecordFaultPage(uint64_t page, uint64_t bytes) noexcept;
// KYTY_WAIT_STATS: appends a diagnostic line to kyty-diag.txt beside the emulator (kept out of the
// console, whose scrollback a busy trace overruns).
void WriteDiag(const char* text) noexcept;
// The reason named by the innermost Scope on this thread, or nullptr.
[[nodiscard]] const char* CurrentReason() noexcept;

// Adds the CPU time spent in its lifetime, minus this thread's GPU waits, under a name
// (KYTY_WAIT_STATS; listed as "CPU time excluding GPU waits").
// Adds CPU time measured elsewhere under a name, as a CpuTimer would.
void RecordCpuTime(const char* name, uint64_t nanoseconds) noexcept;

class CpuTimer {
public:
	explicit CpuTimer(const char* name) noexcept;
	~CpuTimer();
	CpuTimer(const CpuTimer&)            = delete;
	CpuTimer& operator=(const CpuTimer&) = delete;

private:
	const char* m_name;
	bool        m_enabled;
	uint64_t    m_start      = 0;
	uint64_t    m_wait_start = 0;
};

class Scope {
public:
	explicit Scope(const char* reason) noexcept;
	~Scope();
	Scope(const Scope&)            = delete;
	Scope& operator=(const Scope&) = delete;

private:
	const char* m_previous;
};

} // namespace Libs::Graphics::GpuWaitStats

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUWAITSTATS_H_
