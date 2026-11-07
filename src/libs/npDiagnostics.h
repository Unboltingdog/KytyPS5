#ifndef EMULATOR_INCLUDE_EMULATOR_LIBS_NPDIAGNOSTICS_H_
#define EMULATOR_INCLUDE_EMULATOR_LIBS_NPDIAGNOSTICS_H_

#include "common/abi.h"

#include <cstdint>
#include <cstdlib>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <type_traits>

namespace Libs::NpDiagnostics {

inline bool Enabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_DIAG_NP");
		return value != nullptr && value[0] == '1';
	}();
	return enabled;
}

void LogCall(std::string_view name, std::string&& arguments, std::string&& result);
void LogCallback(std::string_view name, std::string&& arguments);

inline std::string FormatString(const char* value) {
	if (value == nullptr) {
		return "null";
	}
	std::string result = "\"";
	size_t length = 0;
	for (; length < 160 && value[length] != '\0'; length++) {
		const char c = value[length];
		switch (c) {
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			case '"': result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			default: result += c; break;
		}
	}
	if (value[length] != '\0') {
		result += "...";
	}
	result += '"';
	return result;
}

template <typename T>
std::string FormatValue(const T& value) {
	using U = std::decay_t<T>;
	if constexpr (std::is_same_v<U, std::nullptr_t>) {
		return "null";
	} else if constexpr (std::is_same_v<U, const char*>) {
		return FormatString(value);
	} else if constexpr (std::is_same_v<U, bool>) {
		return value ? "true" : "false";
	} else if constexpr (std::is_enum_v<U>) {
		return fmt::format("{}", static_cast<std::underlying_type_t<U>>(value));
	} else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>) {
		return fmt::format("{}", value);
	} else if constexpr (std::is_pointer_v<U>) {
		const auto address = reinterpret_cast<uintptr_t>(value);
		if (value == nullptr) {
			return "null";
		}
		using Pointee = std::remove_cv_t<std::remove_pointer_t<U>>;
		if constexpr (std::is_integral_v<Pointee> && !std::is_same_v<Pointee, char>) {
			return fmt::format("0x{:x}={}", address, *value);
		} else if constexpr (std::is_pointer_v<Pointee>) {
			return fmt::format("0x{:x}=0x{:x}", address,
			                   reinterpret_cast<uintptr_t>(*value));
		} else {
			return fmt::format("0x{:x}", address);
		}
	} else {
		return "<argument>";
	}
}

template <typename... Args>
std::string FormatArguments(const Args&... args) {
	std::string result;
	((result += result.empty() ? "" : ", ", result += FormatValue(args)), ...);
	return result;
}

template <auto Function>
struct Export;

template <typename R, typename... Args, R(KYTY_SYSV_ABI*Function)(Args...)>
struct Export<Function> {
	static inline const char* name = "";

	static R KYTY_SYSV_ABI Call(Args... args) {
		if (!Enabled()) {
			if constexpr (std::is_void_v<R>) {
				Function(args...);
				return;
			} else {
				return Function(args...);
			}
		}
		if constexpr (std::is_void_v<R>) {
			Function(args...);
			LogCall(name, FormatArguments(args...), "void");
		} else {
			R result = Function(args...);
			LogCall(name, FormatArguments(args...), FormatValue(result));
			return result;
		}
	}
};

} // namespace Libs::NpDiagnostics

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define NP_DIAGNOSTICS_FUNC(nid, function)                                                        \
	::Libs::NpDiagnostics::Export<function>::name = #function;                                     \
	LIB_FUNC(nid, ::Libs::NpDiagnostics::Export<function>::Call)

#endif /* EMULATOR_INCLUDE_EMULATOR_LIBS_NPDIAGNOSTICS_H_ */
