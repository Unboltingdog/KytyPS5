#ifndef KYTY_COMMON_POLL_CALLS_H_
#define KYTY_COMMON_POLL_CALLS_H_

#include "common/abi.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace Common::PollCalls {

bool Enabled();
void Initialize();
void RegisterWrapped();
void SetAddressResolver(std::string (*resolver)(uint64_t address));
void RegisterCurrentThread(int guest_id, const char* name);
void UnregisterCurrentThread();
void SetCurrentThreadName(const char* name);

uint64_t Wrap(uint64_t target, const char* library, const char* function);
void     RecordResolved(const char* library, const char* module, const char* function, uint64_t caller,
                        uint64_t result, const uint64_t* arguments, size_t argument_count,
                        uint64_t output_mask);
void     RecordUnresolved(const char* function, uint64_t caller);
void     KYTY_SYSV_ABI RecordCall(uint64_t function_id, uint64_t caller, uint64_t result,
                                  const uint64_t* arguments);

namespace Detail {

template <typename T>
uint64_t Value(const T& value) {
	using U = std::decay_t<T>;
	if constexpr (std::is_pointer_v<U>) {
		return reinterpret_cast<uint64_t>(value);
	} else if constexpr (std::is_enum_v<U>) {
		return static_cast<uint64_t>(static_cast<std::underlying_type_t<U>>(value));
	} else if constexpr (std::is_integral_v<U>) {
		return static_cast<uint64_t>(value);
	} else if constexpr (std::is_same_v<U, float>) {
		return std::bit_cast<uint32_t>(value);
	} else if constexpr (std::is_same_v<U, double>) {
		return std::bit_cast<uint64_t>(value);
	} else {
		return 0;
	}
}

template <typename... Args>
consteval uint64_t OutputPointerMask() {
	uint64_t mask = 0;
	size_t index = 0;
	([&mask, &index] {
		using U = std::decay_t<Args>;
		if constexpr (std::is_pointer_v<U> && !std::is_const_v<std::remove_pointer_t<U>>) {
			if (index < 64) {
				mask |= uint64_t {1} << index;
			}
		}
		index++;
	}(), ...);
	return mask;
}

} // namespace Detail

template <auto Function>
struct Export {
	static constexpr bool use_thunk = true;
	static inline const char* library = "";
	static inline const char* module = "";
	static inline const char* name = "";
	static inline auto Call = Function;
};

template <typename R, typename... Args, R(KYTY_SYSV_ABI* Function)(Args...)>
struct Export<Function> {
	static constexpr bool use_thunk = false;
	static inline const char* library = "";
	static inline const char* module = "";
	static inline const char* name = "";

	static R KYTY_SYSV_ABI Call(Args... args) {
		const std::array<uint64_t, sizeof...(Args)> arguments {Detail::Value(args)...};
		const auto caller = reinterpret_cast<uint64_t>(__builtin_return_address(0));
		if constexpr (std::is_void_v<R>) {
			Function(args...);
			RecordResolved(library, module, name, caller, 0, arguments.data(), sizeof...(Args),
			               Detail::OutputPointerMask<Args...>());
			return;
		} else {
			R result = Function(args...);
			RecordResolved(library, module, name, caller, Detail::Value(result), arguments.data(), sizeof...(Args),
			               Detail::OutputPointerMask<Args...>());
			return result;
		}
	}
};

} // namespace Common::PollCalls

#endif
