#include "libs/addcontDiagnostics.h"

#include "common/logging/log.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace Libs::AddContDiagnostics {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t DETAILED_CALL_LIMIT = 200;

std::mutex                                g_mutex;
std::map<std::string, uint64_t, std::less<>> g_counters;
uint64_t                                  g_calls = 0;
Clock::time_point                         g_last_summary = Clock::now();

} // namespace

void LogCall(std::string_view name, std::string&& arguments, std::string&& result) {
	if (!Enabled()) {
		return;
	}

	const auto now = Clock::now();
	bool emit = false;
	bool summary = false;
	std::string summary_text;
	{
		std::scoped_lock lock(g_mutex);
		g_calls++;
		g_counters[std::string(name)]++;
		emit = g_calls <= DETAILED_CALL_LIMIT;
		summary = g_calls > DETAILED_CALL_LIMIT && now - g_last_summary >= std::chrono::seconds(20);
		if (summary) {
			g_last_summary = now;
			for (const auto& [function, count]: g_counters) {
				summary_text += fmt::format(" {}={}", function, count);
			}
		}
	}

	if (emit) {
		LOGF("[ADDCONT_DIAG] %s(%s) = %s\n", std::string(name).c_str(), arguments.c_str(),
		     result.c_str());
	}
	if (summary) {
		LOGF("[ADDCONT_DIAG] counters%s\n", summary_text.c_str());
	}
}

} // namespace Libs::AddContDiagnostics
