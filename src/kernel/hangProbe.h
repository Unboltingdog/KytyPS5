#ifndef EMULATOR_SRC_KERNEL_HANGPROBE_H_
#define EMULATOR_SRC_KERNEL_HANGPROBE_H_

#include <cstdint>
#include <string>

// KYTY_HANG_PROBE=<seconds> (diagnostic, Unboltingdog fork): for a game that stops making progress
// without crashing. Writes hang-probe.txt next to the emulator:
// - after <seconds>, and again every <seconds>, where every guest thread is executing: several
//   samples of its instruction pointer (a spinning thread moves, a blocked one does not), the code
//   bytes around it, and the guest return addresses on its stack (Windows; elsewhere the samples
//   are skipped);
// - any GPU queue that has waited (WAIT_REG_MEM, memory semaphore) on the same memory for more
//   than 3 s: the address, the value there and what the wait expects.
namespace Kernel::HangProbe {

[[nodiscard]] bool Enabled();

// A guest thread starts (on that thread) or ends.
void ThreadStarted(int unique_id, const std::string& name, uint64_t host_thread_id,
                   uint64_t stack_low, uint64_t stack_high);
void ThreadEnded(int unique_id);

// A guest module occupies [base, base + size).
void ModuleLoaded(const std::string& name, uint64_t base, uint64_t size);

// A GPU queue's wait on `address` did not pass (`passed` false), or passed. `kind` names the wait;
// `queue` the queue (its interrupt event id).
void GpuWait(int queue, const char* kind, const void* address, uint64_t value, uint64_t reference,
             uint64_t mask, uint32_t function, bool passed);

} // namespace Kernel::HangProbe

#endif // EMULATOR_SRC_KERNEL_HANGPROBE_H_
