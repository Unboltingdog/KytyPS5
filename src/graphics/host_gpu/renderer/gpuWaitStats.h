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
// Counts one occurrence of a named event (shown as events=N).
void               RecordEvent(const char* name, uint64_t count = 1) noexcept;
// Counts a CPU readback fault at a page, with the size read back; the busiest pages are listed.
void               RecordFaultPage(uint64_t page, uint64_t bytes) noexcept;
// The reason named by the innermost Scope on this thread, or nullptr.
[[nodiscard]] const char* CurrentReason() noexcept;

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
