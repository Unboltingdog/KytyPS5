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
