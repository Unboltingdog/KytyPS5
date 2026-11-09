#ifndef KYTY_COMMON_GUEST_WAITS_H_
#define KYTY_COMMON_GUEST_WAITS_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Common::GuestWaits {

inline std::atomic_bool& EnabledFlag() {
	static std::atomic_bool enabled {false};
	return enabled;
}

inline bool Enabled() {
	return EnabledFlag().load(std::memory_order_relaxed);
}

void Initialize();
void Dump(size_t call_count);
// KYTY_DIAG_GUEST_THREADS=<seconds>: per-thread waits plus file, AMPR, equeue and GPU-write activity; off when unset.
[[nodiscard]] bool GuestThreadsEnabled();
void RecordFileOpen(const char* guest_path);
void RecordFileRead(const char* guest_path, uint64_t bytes);
void RecordAmprSubmitted();
void RecordAmprCompleted();
void RecordEqueueEvent(int16_t filter);
void RecordGpuWrite(const char* packet, uint32_t queue, uint64_t address, uint64_t value, uint64_t size);
void SetAddressResolver(std::string (*resolver)(uint64_t address));
void RegisterCurrentThread(int guest_id, int host_id, const char* name, uint64_t system_id = 0);
void UnregisterCurrentThread();
void SetCurrentThreadName(const char* name);
void RecordLibCall(const char* library, const char* module, const char* function, uint64_t caller);

class WaitScope {
public:
	WaitScope(const char* kind, const char* object_name, uintptr_t object, uint64_t caller = 0);
	~WaitScope();

	WaitScope(const WaitScope&)            = delete;
	WaitScope& operator=(const WaitScope&) = delete;

	[[nodiscard]] bool IsActive() const { return m_active; }
	void               SetDetails(std::string details);

private:
	bool m_active = false;
};

} // namespace Common::GuestWaits

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GUEST_WAIT_DETAILS(scope, ...)                                                               \
	do {                                                                                           \
		if ((scope).IsActive()) {                                                                  \
			(scope).SetDetails(__VA_ARGS__);                                                       \
		}                                                                                          \
	} while (false)

#endif
