#ifndef EMULATOR_INCLUDE_EMULATOR_LIBS_LIBS_H_
#define EMULATOR_INCLUDE_EMULATOR_LIBS_LIBS_H_

#include "common/abi.h"
#include "common/logging/log.h"
#include "common/stringUtils.h"
#include "common/threads.h"
#include "loader/timer.h" // IWYU pragma: keep

#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <fmt/format.h>

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PRINT_NAME_ENABLED g_print_name

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PRINT_NAME_ENABLE(flag) PRINT_NAME_ENABLED = flag;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_DEFINE(name) void name(Loader::SymbolDatabase* s)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_NAME(l, m)                                                                             \
	[[maybe_unused]] static thread_local bool PRINT_NAME_ENABLED = Libs::TraceAllCalls();          \
	static constexpr char                     g_library[]        = l;                              \
	static constexpr char                     g_module[]         = m;
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_VERSION(l, lv, m, mv1, mv2)                                                            \
	LIB_NAME(l, m);                                                                                \
	static constexpr int g_library_version      = lv;                                              \
	static constexpr int g_module_version_major = mv1;                                             \
	static constexpr int g_module_version_minor = mv2;
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_ADD_ADDRESS(n, address, f, t)                                                          \
	{                                                                                              \
		Loader::SymbolResolve sr {};                                                               \
		sr.name                 = n;                                                               \
		sr.library              = g_library;                                                       \
		sr.library_version      = g_library_version;                                               \
		sr.module               = g_module;                                                        \
		sr.module_version_major = g_module_version_major;                                          \
		sr.module_version_minor = g_module_version_minor;                                          \
		sr.type                 = t;                                                               \
		auto        func        = (address);                                                       \
		const char* dbg_name    = "" #f;                                                           \
		s->Add(sr, func, dbg_name);                                                                \
	}
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_ADD(n, f, t) LIB_ADD_ADDRESS(n, reinterpret_cast<uint64_t>(f), f, t)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_OBJECT(n, f) LIB_ADD(n, f, Loader::SymbolType::Object)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LIB_FUNC(n, f)                                                                             \
	LIB_ADD_ADDRESS(n,                                                                             \
	                (Libs::TraceAllCalls() ? Libs::TraceWrap<f, #f>::Address()                    \
	                                       : reinterpret_cast<uint64_t>(f)),                      \
	                f, Loader::SymbolType::Func)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PRINT_NAME()                                                                               \
	if (PRINT_NAME_ENABLED) {                                                                      \
		Libs::PrintName(g_library, g_module, __func__);                                              \
	}

namespace Loader {
class SymbolDatabase;
} // namespace Loader

namespace Libs {

// KYTY_TRACE_CALLS=1 (diagnostic, Unboltingdog fork): every library call is logged, not only the
// ones a library turns on, so a hang shows the last call each guest thread made. Needs the log
// on (--printf-direction File or Console).
[[nodiscard]] inline bool TraceAllCalls() {
	static const bool trace = [] {
		const char* value = std::getenv("KYTY_TRACE_CALLS");
		return value != nullptr && *value == '1';
	}();
	return trace;
}

[[gnu::noinline]] inline void PrintResult(const char* function, int64_t result) {
	if (Log::GetDirection() != Log::Direction::Silent) {
		LOGF("[%d] %s -> 0x%" PRIx64 " (%" PRId64 ")\n", Common::Thread::GetThreadIdUnique(),
		     function, static_cast<uint64_t>(result), result);
	}
}

// A function's name as a template argument.
template <size_t N>
struct TraceName {
	char text[N] {};
	constexpr TraceName(const char (&name)[N]) { // NOLINT(google-explicit-constructor)
		for (size_t i = 0; i < N; i++) text[i] = name[i];
	}
};

// KYTY_TRACE_CALLS: a library function registered through a thunk that logs what it returned (an
// integer other than 0). Functions it does not forward (variadic ones, other calling conventions,
// structures by value) are registered as they are.
template <auto F, TraceName Name>
struct TraceWrap {
	static uint64_t Address() { return reinterpret_cast<uint64_t>(F); }
};
template <typename R, typename... A, R(KYTY_SYSV_ABI* F)(A...), TraceName Name>
    requires((std::is_void_v<R> || std::is_scalar_v<R>) &&
             ((std::is_scalar_v<A> || std::is_reference_v<A>) && ...))
struct TraceWrap<F, Name> {
	static R KYTY_SYSV_ABI Call(A... args) {
		if constexpr (std::is_void_v<R>) {
			F(args...);
		} else {
			R result = F(args...);
			if constexpr (std::is_integral_v<R>) {
				if (result != 0) {
					PrintResult(Name.text, static_cast<int64_t>(result));
				}
			}
			return result;
		}
	}
	static uint64_t Address() { return reinterpret_cast<uint64_t>(&Call); }
};

// Keep the formatting path from inflating fiber functions' stack frames under LTO.
[[gnu::noinline]] inline void PrintName(const char* library, const char* module, const char* function) {
	if (Log::GetDirection() != Log::Direction::Silent) {
		const auto elapsed_ms      = static_cast<uint64_t>(Loader::Timer::GetTimeMs());
		const auto print_name_time = fmt::format(
		    "{:02}:{:02}:{:02}.{:03}", elapsed_ms / 3600000, (elapsed_ms / 60000) % 60,
		    (elapsed_ms / 1000) % 60, elapsed_ms % 1000);
		LOGF_COLOR(Log::Color::Cyan, "[%d][%s] %s::%s::%s()\n",
		           Common::Thread::GetThreadIdUnique(), print_name_time.c_str(), library, module,
		           function);
	}
}

void InitAll(Loader::SymbolDatabase* s);

} // namespace Libs
#endif /* EMULATOR_INCLUDE_EMULATOR_LIBS_LIBS_H_ */
