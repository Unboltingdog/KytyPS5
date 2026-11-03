#ifndef EMULATOR_SRC_COMMON_GUESTWAITDIAGNOSTICS_H_
#define EMULATOR_SRC_COMMON_GUESTWAITDIAGNOSTICS_H_

#include "common/diagLog.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <fmt/format.h>

namespace Common::GuestWaitDiagnostics {

constexpr uint64_t InfiniteTimeout = std::numeric_limits<uint64_t>::max();
constexpr uint64_t ReportAfterNs   = 5'000'000'000ull;

[[nodiscard]] inline bool Enabled() {
	const char* value = std::getenv("KYTY_WAIT_STATS");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

[[nodiscard]] inline uint64_t NowNs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
	                                 std::chrono::steady_clock::now().time_since_epoch())
	                                 .count());
}

inline void LogSlow(const char* name, uint64_t start_ns, uint64_t timeout_us, uint64_t id,
                    const char* outcome, int result, const void* return_address) noexcept {
	if (!Enabled() || start_ns == 0) return;
	const auto elapsed_ns = NowNs() - start_ns;
	if (elapsed_ns <= ReportAfterNs) return;
	const auto timeout = timeout_us == InfiniteTimeout ? std::string("inf")
	                                                   : fmt::format("{}", timeout_us);
	DiagLog::Write(fmt::format(
	                   "guest-wait name={} duration_ms={} timeout_us={} outcome={} result={} "
	                   "id=0x{:016x} return={} thread={}\n",
	                   name, elapsed_ns / 1'000'000u, timeout, outcome, result, id, return_address,
	                   std::hash<std::thread::id> {}(std::this_thread::get_id()))
	                   .c_str());
}

} // namespace Common::GuestWaitDiagnostics

#endif // EMULATOR_SRC_COMMON_GUESTWAITDIAGNOSTICS_H_
