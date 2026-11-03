#ifndef EMULATOR_SRC_COMMON_DIAGLOG_H_
#define EMULATOR_SRC_COMMON_DIAGLOG_H_

#include <cstdio>
#include <mutex>

namespace Common::DiagLog {

inline void Write(const char* text) noexcept {
	static std::mutex mutex;
	static std::FILE* file = std::fopen("kyty-diag.txt", "w");
	if (file == nullptr) return;
	std::scoped_lock lock {mutex};
	std::fputs(text, file);
	std::fflush(file);
}

} // namespace Common::DiagLog

#endif // EMULATOR_SRC_COMMON_DIAGLOG_H_
