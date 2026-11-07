#ifndef KYTY_COMMON_POLL_CALLS_H_
#define KYTY_COMMON_POLL_CALLS_H_

#include "common/abi.h"

#include <cstdint>
#include <string>

namespace Common::PollCalls {

bool Enabled();
void Initialize();
void SetAddressResolver(std::string (*resolver)(uint64_t address));
void RegisterCurrentThread(int guest_id, const char* name);
void UnregisterCurrentThread();
void SetCurrentThreadName(const char* name);

uint64_t Wrap(uint64_t target, const char* library, const char* function);
void     RecordUnresolved(const char* function, uint64_t caller);
void     KYTY_SYSV_ABI RecordCall(uint64_t function_id, uint64_t caller, uint64_t result,
                                  const uint64_t* arguments);

} // namespace Common::PollCalls

#endif
