#include "common/guestWaits.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Common::GuestWaits {
namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t CALL_HISTORY_SIZE = 32;

struct ThreadRecord {
	std::mutex                          mutex;
	int                                 guest_id = 0;
	int                                 host_id  = 0;
	std::string                         name;
	bool                                blocked = false;
	std::string                         kind;
	std::string                         object_name;
	uintptr_t                           object = 0;
	uint64_t                            caller = 0;
	Clock::time_point                   blocked_since {};
	std::string                         details;
	uint64_t                            last_caller = 0;
	std::array<std::string, CALL_HISTORY_SIZE> calls;
	size_t                              next_call = 0;
	size_t                              call_count = 0;
};

std::mutex                                      g_registry_mutex;
std::vector<std::shared_ptr<ThreadRecord>>      g_records;
thread_local std::shared_ptr<ThreadRecord>      g_current_record;
std::once_flag                                  g_worker_once;
std::string                                   (*g_address_resolver)(uint64_t) = nullptr;
Clock::time_point                               g_start;
int64_t                                         g_once_at_ms = -1;
bool                                            g_periodic = false;

std::string CleanField(std::string_view value) {
	std::string result(value);
	std::replace_if(
	    result.begin(), result.end(), [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }, '_');
	return result.empty() ? std::string("-") : result;
}

std::string Hex(uint64_t value) {
	char buffer[32] {};
	std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
	return buffer;
}

std::string CallerText(uint64_t caller) {
	if (caller == 0) {
		return "unknown";
	}
	if (g_address_resolver != nullptr) {
		return g_address_resolver(caller);
	}
	return Hex(caller);
}

void EnsureWorker() {
	std::call_once(g_worker_once, [] {
		std::thread([] {
			bool once_done = false;
			auto next_periodic = g_start + std::chrono::seconds(20);
			while (g_periodic || !once_done) {
				const auto now = Clock::now();
				if (g_once_at_ms >= 0 && !once_done &&
				    now - g_start >= std::chrono::milliseconds(g_once_at_ms)) {
					Dump(CALL_HISTORY_SIZE);
					once_done = true;
				}
				if (g_periodic && now >= next_periodic) {
					Dump(4);
					next_periodic = now + std::chrono::seconds(20);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}).detach();
	});
}

} // namespace

void Dump(size_t call_count) {
	std::vector<std::shared_ptr<ThreadRecord>> records;
	{
		std::lock_guard lock(g_registry_mutex);
		records = g_records;
	}

	const auto now = Clock::now();
	std::string output = "GUEST_WAIT_DUMP elapsed_ms=" +
	                     std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start).count()) +
	                     " threads=" + std::to_string(records.size()) + "\n";
	for (const auto& record: records) {
		int guest_id = 0;
		int host_id = 0;
		std::string name;
		bool blocked = false;
		std::string kind;
		std::string object_name;
		uintptr_t object = 0;
		uint64_t caller = 0;
		uint64_t blocked_ms = 0;
		std::string details;
		std::string calls;
		{
			std::lock_guard lock(record->mutex);
			guest_id = record->guest_id;
			host_id = record->host_id;
			name = record->name;
			blocked = record->blocked;
			kind = record->kind;
			object_name = record->object_name;
			object = record->object;
			caller = record->caller;
			if (blocked) {
				blocked_ms = static_cast<uint64_t>(
				    std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(now - record->blocked_since).count()));
			}
			details = record->details;
			const size_t count = std::min(call_count, record->call_count);
			for (size_t i = 0; i < count; i++) {
				const size_t index = (record->next_call + CALL_HISTORY_SIZE - count + i) % CALL_HISTORY_SIZE;
				if (!calls.empty()) {
					calls += " | ";
				}
				calls += record->calls[index];
			}
		}

		output += "GUEST_WAIT tid=" + std::to_string(guest_id) + " host_tid=" + std::to_string(host_id) +
		          " name=" + CleanField(name) + " state=" + (blocked ? "blocked" : "running") + " on=" +
		          (blocked ? CleanField(kind) : "none") + " obj=" + CleanField(object_name) + "@" +
		          Hex(object) + " blocked_ms=" + std::to_string(blocked_ms) + " caller=" + CallerText(caller) +
		          " last_calls=" + (calls.empty() ? std::string("-") : calls);
		if (!details.empty()) {
			output += " details=" + details;
		}
		output += "\n";
	}
	std::fwrite(output.data(), 1, output.size(), stderr);
	std::fflush(stderr);
}

void Initialize() {
	g_periodic = [] {
		const char* value = std::getenv("KYTY_DIAG_GUEST_WAITS");
		return value != nullptr && value[0] == '1';
	}();
	g_once_at_ms = [] {
		const char* value = std::getenv("KYTY_DIAG_GUEST_WAITS_ONCE_AT");
		if (value == nullptr || value[0] == '\0') {
			return int64_t {-1};
		}
		const double seconds = std::strtod(value, nullptr);
		return std::isfinite(seconds) && seconds >= 0 ? static_cast<int64_t>(seconds * 1000.0) : int64_t {-1};
	}();
	g_start = Clock::now();
	EnabledFlag().store(g_periodic || g_once_at_ms >= 0, std::memory_order_release);
}

void SetAddressResolver(std::string (*resolver)(uint64_t address)) {
	g_address_resolver = resolver;
}

void RegisterCurrentThread(int guest_id, int host_id, const char* name) {
	if (!Enabled()) {
		return;
	}
	auto record = std::make_shared<ThreadRecord>();
	record->guest_id = guest_id;
	record->host_id = host_id;
	record->name = name != nullptr ? name : "";
	g_current_record = record;
	{
		std::lock_guard lock(g_registry_mutex);
		g_records.push_back(record);
	}
	EnsureWorker();
}

void UnregisterCurrentThread() {
	if (g_current_record == nullptr) {
		return;
	}
	std::lock_guard lock(g_registry_mutex);
	std::erase(g_records, g_current_record);
	g_current_record.reset();
}

void SetCurrentThreadName(const char* name) {
	if (g_current_record == nullptr) {
		return;
	}
	std::lock_guard lock(g_current_record->mutex);
	g_current_record->name = name != nullptr ? name : "";
}

void RecordLibCall(const char* library, const char* module, const char* function, uint64_t caller) {
	if (g_current_record == nullptr) {
		return;
	}
	auto& record = *g_current_record;
	std::lock_guard lock(record.mutex);
	record.last_caller = caller;
	record.calls[record.next_call] = std::string(library) + "::" + module + "::" + function + "()";
	record.next_call = (record.next_call + 1) % CALL_HISTORY_SIZE;
	record.call_count = std::min(record.call_count + 1, CALL_HISTORY_SIZE);
}

WaitScope::WaitScope(const char* kind, const char* object_name, uintptr_t object, uint64_t caller) {
	if (!Enabled() || g_current_record == nullptr) {
		return;
	}
	auto& record = *g_current_record;
	std::lock_guard lock(record.mutex);
	record.blocked = true;
	record.kind = kind;
	record.object_name = object_name != nullptr ? object_name : "";
	record.object = object;
	record.caller = caller != 0 ? caller : record.last_caller;
	record.blocked_since = Clock::now();
	record.details.clear();
	m_active = true;
}

WaitScope::~WaitScope() {
	if (!m_active) {
		return;
	}
	auto& record = *g_current_record;
	std::lock_guard lock(record.mutex);
	record.blocked = false;
	record.kind.clear();
	record.object_name.clear();
	record.object = 0;
	record.caller = 0;
	record.details.clear();
}

void WaitScope::SetDetails(std::string details) {
	if (!m_active) {
		return;
	}
	std::lock_guard lock(g_current_record->mutex);
	g_current_record->details = std::move(details);
}

} // namespace Common::GuestWaits
