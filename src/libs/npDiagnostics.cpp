#include "libs/npDiagnostics.h"

#include "common/logging/log.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace Libs::NpDiagnostics {
namespace {

using Clock = std::chrono::steady_clock;

struct Counter {
	uint64_t          calls = 0;
	Clock::time_point last_log {};
};

std::mutex                  g_mutex;
std::map<std::string, Counter, std::less<>> g_counters;
Clock::time_point           g_last_summary = Clock::now();

void Record(std::string_view name, std::string&& arguments, std::string&& result,
            bool callback) {
	if (!Enabled()) {
		return;
	}

	const auto now = Clock::now();
	bool emit = false;
	bool summary = false;
	std::string summary_text;
	{
		std::scoped_lock lock(g_mutex);
		auto& counter = g_counters[std::string(name)];
		counter.calls++;
		emit = now - counter.last_log >= std::chrono::seconds(1);
		if (emit) {
			counter.last_log = now;
		}
		summary = now - g_last_summary >= std::chrono::seconds(20);
		if (summary) {
			g_last_summary = now;
			for (const auto& [function, count]: g_counters) {
				summary_text += fmt::format(" {}={}", function, count.calls);
			}
		}
	}

	if (emit) {
		if (callback) {
			LOGF("[NP_DIAG] callback %s(%s)\n", std::string(name).c_str(), arguments.c_str());
		} else {
			LOGF("[NP_DIAG] %s(%s) = %s\n", std::string(name).c_str(), arguments.c_str(),
			     result.c_str());
		}
	}
	if (summary) {
		LOGF("[NP_DIAG] counters%s\n", summary_text.c_str());
	}
}

} // namespace

void LogCall(std::string_view name, std::string&& arguments, std::string&& result) {
	Record(name, std::move(arguments), std::move(result), false);
}

void LogCallback(std::string_view name, std::string&& arguments) {
	Record(name, std::move(arguments), {}, true);
}

} // namespace Libs::NpDiagnostics
