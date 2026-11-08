#ifndef KYTY_COMMON_CALL_TRACE_H_
#define KYTY_COMMON_CALL_TRACE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Common::CallTrace {

bool Enabled();
bool Active();
void Initialize();
void SetAddressResolver(std::string (*resolver)(uint64_t address));
void RegisterCurrentThread(int guest_id, const char* name);
void UnregisterCurrentThread();
void SetCurrentThreadName(const char* name);

void RecordCall(std::string_view function, uint64_t caller, uint64_t result, const uint64_t* arguments,
                size_t argument_count, uint64_t output_mask);
void Event(std::string_view kind, std::string_view details);

} // namespace Common::CallTrace

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define KYTY_CALL_TRACE_EVENT(kind, details)                                                            \
	do {                                                                                                \
		if (::Common::CallTrace::Active()) {                                                            \
			::Common::CallTrace::Event(kind, details);                                                   \
		}                                                                                               \
	} while (false)

#endif
