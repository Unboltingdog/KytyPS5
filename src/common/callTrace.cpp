#include "common/callTrace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Common::CallTrace {
namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t RING_SIZE         = 512;
constexpr size_t MAX_OUTPUTS       = 8;
constexpr size_t OUT_CAPTURE_SIZE  = 32;
constexpr uint64_t DEFAULT_MAX     = 2000000;
constexpr int64_t DEFAULT_FOR_MS   = 30000;

struct OutputCapture {
	uint8_t index = 0;
	bool valid = false;
	std::array<uint8_t, OUT_CAPTURE_SIZE> bytes {};
};

enum class RecordKind : uint8_t {
	Call,
	Event,
};

struct TraceRecord {
	uint64_t sequence = 0;
	uint64_t time_ms = 0;
	uint64_t caller = 0;
	uint64_t result = 0;
	int guest_id = 0;
	RecordKind kind = RecordKind::Call;
	std::array<char, 128> function {};
	std::array<char, 48> thread_name {};
	std::array<uint64_t, 4> arguments {};
	uint8_t output_count = 0;
	std::array<OutputCapture, MAX_OUTPUTS> outputs {};
	std::array<char, 384> details {};
};

struct ThreadBuffer {
	std::atomic_uint64_t head {0};
	std::atomic_uint64_t tail {0};
	int guest_id = 0;
	std::array<char, 48> name {};
	std::array<TraceRecord, RING_SIZE> records;
};

Clock::time_point g_start = Clock::now();
int64_t g_for_ms = DEFAULT_FOR_MS;
uint64_t g_max_lines = DEFAULT_MAX;
std::atomic_bool g_active {false};
std::atomic_uint64_t g_next_sequence {1};
std::atomic_uint64_t g_lines {0};
std::atomic_uint64_t g_dropped {0};
std::atomic_uint64_t g_calls {0};
std::atomic_uint64_t g_events {0};
std::string (*g_address_resolver)(uint64_t address) = nullptr;
std::mutex g_registry_mutex;
std::vector<std::shared_ptr<ThreadBuffer>> g_threads;
thread_local std::shared_ptr<ThreadBuffer> g_current_thread;

void CopyText(std::string_view source, char* destination, size_t size) {
	if (size == 0) {
		return;
	}
	const size_t count = std::min(source.size(), size - 1);
	std::copy_n(source.data(), count, destination);
	destination[count] = '\0';
}

template <size_t N>
void CopyText(std::string_view source, std::array<char, N>* destination) {
	CopyText(source, destination->data(), destination->size());
}

int64_t NowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_start).count();
}

bool ReadGuestBytes(uint64_t address, void* destination, size_t size) {
	if (address == 0) {
		return false;
	}
#if defined(_WIN32)
	SIZE_T read = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, size, &read) &&
	       read == size;
#elif defined(__APPLE__)
	mach_vm_size_t read = 0;
	return mach_vm_read_overwrite(mach_task_self(), address, size,
	                              reinterpret_cast<mach_vm_address_t>(destination), &read) == KERN_SUCCESS &&
	       read == size;
#else
	iovec local {.iov_base = destination, .iov_len = size};
	iovec remote {.iov_base = reinterpret_cast<void*>(address), .iov_len = size};
	return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
#endif
}

std::string Timestamp() {
	if (const char* value = std::getenv("KYTY_LOG_TIMESTAMP"); value != nullptr && value[0] != '\0') {
		std::string result(value);
		const bool safe = result.size() <= 64 &&
		                  std::ranges::all_of(result, [](unsigned char c) {
			                  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
			                         (c >= 'a' && c <= 'z') || c == '-' || c == '_';
		                  });
		if (safe) {
			return result;
		}
	}
	const std::time_t now = std::time(nullptr);
	std::tm local {};
#if defined(_WIN32)
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char buffer[32] {};
	std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
	return buffer;
}

std::shared_ptr<ThreadBuffer> RegisterThread(int guest_id, const char* name) {
	auto record = std::make_shared<ThreadBuffer>();
	record->guest_id = guest_id;
	CopyText(name != nullptr ? name : "host", &record->name);
	g_current_thread = record;
	std::lock_guard lock(g_registry_mutex);
	g_threads.push_back(record);
	return record;
}

std::shared_ptr<ThreadBuffer> CurrentThread() {
	if (g_current_thread == nullptr) {
		RegisterThread(0, "host");
	}
	return g_current_thread;
}

bool Enqueue(TraceRecord&& record) {
	if (!g_active.load(std::memory_order_relaxed) ||
	    g_lines.load(std::memory_order_relaxed) >= g_max_lines) {
		return false;
	}
	auto thread = CurrentThread();
	const uint64_t head = thread->head.load(std::memory_order_relaxed);
	const uint64_t tail = thread->tail.load(std::memory_order_acquire);
	if (head - tail >= RING_SIZE) {
		g_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	record.sequence = g_next_sequence.fetch_add(1, std::memory_order_relaxed);
	record.time_ms = static_cast<uint64_t>(std::max<int64_t>(0, NowMs()));
	record.guest_id = thread->guest_id;
	CopyText(std::string_view(thread->name.data()), &record.thread_name);
	thread->records[head % RING_SIZE] = std::move(record);
	thread->head.store(head + 1, std::memory_order_release);
	return true;
}

uint64_t RequestedOutputs(std::string_view name, uint64_t typed_mask) {
	if (name.ends_with("PthreadCondTimedwait")) {
		return 0;
	}
	if (name.ends_with("KernelWaitEqueue")) {
		return typed_mask != 0 ? typed_mask : (uint64_t {1} << 1) | (uint64_t {1} << 3);
	}
	if (name.ends_with("KernelTriggerUserEvent")) {
		return uint64_t {1} << 2;
	}
	if (name.ends_with("KernelStat") || name.ends_with("KernelFstat") || name.ends_with("KernelRead") ||
	    name.ends_with("KernelPread")) {
		return uint64_t {1} << 1;
	}
	if (name.find("SaveData") != std::string_view::npos ||
	    name.find("AppContent") != std::string_view::npos ||
	    name.find("NpEntitlement") != std::string_view::npos) {
		return typed_mask;
	}
	return 0;
}

std::string CleanField(std::string_view value) {
	std::string result(value);
	std::replace_if(result.begin(), result.end(), [](char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
	}, '_');
	return result.empty() ? "-" : result;
}

std::string BytesText(const std::array<uint8_t, OUT_CAPTURE_SIZE>& bytes) {
	static constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(bytes.size() * 2);
	for (const uint8_t byte: bytes) {
		result.push_back(digits[byte >> 4]);
		result.push_back(digits[byte & 0xf]);
	}
	return result;
}

std::string CallerText(uint64_t caller) {
	if (caller == 0) {
		return "unknown";
	}
	return g_address_resolver != nullptr ? g_address_resolver(caller) : fmt::format("0x{:x}", caller);
}

struct PendingLine {
	bool valid = false;
	uint64_t count = 0;
	TraceRecord record;
};

std::string Format(const TraceRecord& record, uint64_t count) {
	std::string output;
	if (record.kind == RecordKind::Event) {
		output = fmt::format("t={} tid={} thread={} event={}", record.time_ms, record.guest_id,
		                     CleanField(record.thread_name.data()), CleanField(record.function.data()));
		if (record.details[0] != '\0') {
			output += ' ';
			output += CleanField(record.details.data());
		}
	} else {
		output = fmt::format("t={} tid={} thread={} fn={} caller={} a=0x{:x},0x{:x},0x{:x},0x{:x} ret=0x{:x}",
		                     record.time_ms, record.guest_id, CleanField(record.thread_name.data()),
		                     CleanField(record.function.data()), CleanField(CallerText(record.caller)),
		                     record.arguments[0], record.arguments[1], record.arguments[2], record.arguments[3],
		                     record.result);
		for (size_t i = 0; i < record.output_count; i++) {
			const auto& capture = record.outputs[i];
			output += fmt::format(" out{}={}", capture.index,
			                      capture.valid ? BytesText(capture.bytes) : "unavailable");
		}
	}
	if (count > 1) {
		output += fmt::format(" x{}", count);
	}
	output += '\n';
	return output;
}

void FlushPending(FILE* file, PendingLine* pending) {
	if (!pending->valid) {
		return;
	}
	if (g_lines.load(std::memory_order_relaxed) < g_max_lines) {
		const std::string line = Format(pending->record, pending->count);
		std::fwrite(line.data(), 1, line.size(), file);
		g_lines.fetch_add(1, std::memory_order_relaxed);
	}
	pending->valid = false;
}

bool SameCall(const TraceRecord& a, const TraceRecord& b) {
	return a.kind == RecordKind::Call && b.kind == RecordKind::Call && a.guest_id == b.guest_id &&
	       a.caller == b.caller && a.result == b.result &&
	       std::string_view(a.function.data()) == std::string_view(b.function.data()) &&
	       std::string_view(a.thread_name.data()) == std::string_view(b.thread_name.data());
}

enum class DrainResult {
	Empty,
	Waiting,
	Record,
};

DrainResult DrainOne(FILE* file, PendingLine* pending) {
	std::vector<std::shared_ptr<ThreadBuffer>> threads;
	{
		std::lock_guard lock(g_registry_mutex);
		threads = g_threads;
	}
	static uint64_t next_sequence = 1;
	bool waiting = false;
	for (const auto& thread: threads) {
		const uint64_t tail = thread->tail.load(std::memory_order_relaxed);
		const uint64_t head = thread->head.load(std::memory_order_acquire);
		if (tail == head) {
			continue;
		}
		const auto& candidate = thread->records[tail % RING_SIZE];
		if (candidate.sequence != next_sequence) {
			waiting = waiting || candidate.sequence > next_sequence;
			continue;
		}
		TraceRecord record = candidate;
		thread->tail.store(tail + 1, std::memory_order_release);
		next_sequence++;
		if (pending->valid && SameCall(pending->record, record)) {
			pending->count++;
		} else {
			FlushPending(file, pending);
			pending->valid = true;
			pending->count = 1;
			pending->record = std::move(record);
		}
		return DrainResult::Record;
	}
	return waiting ? DrainResult::Waiting : DrainResult::Empty;
}

void Writer(FILE* file) {
	PendingLine pending;
	const auto deadline = g_start + std::chrono::milliseconds(g_for_ms);
	while (Clock::now() < deadline) {
		if (DrainOne(file, &pending) == DrainResult::Record) {
			continue;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	g_active.store(false, std::memory_order_release);
	std::this_thread::sleep_until(deadline + std::chrono::milliseconds(50));
	while (DrainOne(file, &pending) == DrainResult::Record) {
	}
	FlushPending(file, &pending);
	std::fflush(file);
	std::fclose(file);
	std::fprintf(stderr, "CALL_TRACE closed lines=%llu calls=%llu events=%llu dropped=%llu\n",
	             static_cast<unsigned long long>(g_lines.load(std::memory_order_relaxed)),
	             static_cast<unsigned long long>(g_calls.load(std::memory_order_relaxed)),
	             static_cast<unsigned long long>(g_events.load(std::memory_order_relaxed)),
	             static_cast<unsigned long long>(g_dropped.load(std::memory_order_relaxed)));
}

} // namespace

bool Enabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_DIAG_CALL_TRACE");
		return value != nullptr && value[0] == '1';
	}();
	return enabled;
}

bool Active() {
	return g_active.load(std::memory_order_relaxed);
}

void Initialize() {
	if (!Enabled()) {
		return;
	}
	g_start = Clock::now();
	g_for_ms = [] {
		const char* value = std::getenv("KYTY_DIAG_CALL_TRACE_FOR");
		const double seconds = value != nullptr ? std::strtod(value, nullptr) : 30.0;
		return std::isfinite(seconds) && seconds >= 0 ? static_cast<int64_t>(seconds * 1000.0) : DEFAULT_FOR_MS;
	}();
	g_max_lines = [] {
		const char* value = std::getenv("KYTY_DIAG_CALL_TRACE_MAX");
		return value != nullptr && value[0] != '\0' ? std::strtoull(value, nullptr, 10) : DEFAULT_MAX;
	}();
	const std::string path = "_Logs/calltrace-" + Timestamp() + ".txt";
	std::error_code error;
	std::filesystem::create_directories("_Logs", error);
	FILE* file = std::fopen(path.c_str(), "w");
	if (file == nullptr) {
		std::fprintf(stderr, "CALL_TRACE open_failed path=%s\n", path.c_str());
		return;
	}
	g_active.store(true, std::memory_order_release);
	std::fprintf(stderr, "CALL_TRACE opened path=%s for_ms=%lld max=%llu\n", path.c_str(),
	             static_cast<long long>(g_for_ms), static_cast<unsigned long long>(g_max_lines));
	std::thread([file] { Writer(file); }).detach();
}

void SetAddressResolver(std::string (*resolver)(uint64_t address)) {
	g_address_resolver = resolver;
}

void RegisterCurrentThread(int guest_id, const char* name) {
	if (Enabled()) {
		RegisterThread(guest_id, name);
	}
}

void UnregisterCurrentThread() {
	g_current_thread.reset();
}

void SetCurrentThreadName(const char* name) {
	if (g_current_thread != nullptr) {
		CopyText(name != nullptr ? name : "", &g_current_thread->name);
	}
}

// KYTY_DIAG_CALL_TRACE_SKIP=Name1,Name2: no call records from guest threads with these names (busy pollers
// such as audio workers would otherwise fill the line cap within seconds). Events are still recorded.
static bool SkippedThread() {
	static const std::vector<std::string> skipped = [] {
		std::vector<std::string> names;
		const char*              value = std::getenv("KYTY_DIAG_CALL_TRACE_SKIP");
		std::string_view         list  = value != nullptr ? value : "";
		while (!list.empty()) {
			const auto comma = list.find(',');
			const auto item  = list.substr(0, comma);
			if (!item.empty()) names.emplace_back(item);
			if (comma == std::string_view::npos) break;
			list.remove_prefix(comma + 1);
		}
		return names;
	}();
	if (skipped.empty() || g_current_thread == nullptr) {
		return false;
	}
	const std::string_view name(g_current_thread->name.data());
	return std::find(skipped.begin(), skipped.end(), name) != skipped.end();
}

void RecordCall(std::string_view function, uint64_t caller, uint64_t result, const uint64_t* arguments,
                size_t argument_count, uint64_t output_mask) {
	if (!Active() || SkippedThread()) {
		return;
	}
	TraceRecord record;
	CopyText(function, &record.function);
	record.caller = caller;
	record.result = result;
	for (size_t i = 0; i < record.arguments.size() && i < argument_count; i++) {
		record.arguments[i] = arguments != nullptr ? arguments[i] : 0;
	}
	const uint64_t wanted = RequestedOutputs(function, output_mask);
	for (size_t i = 0; i < 64 && record.output_count < MAX_OUTPUTS; i++) {
		if ((wanted & (uint64_t {1} << i)) == 0) {
			continue;
		}
		auto& capture = record.outputs[record.output_count++];
		capture.index = static_cast<uint8_t>(i);
		capture.valid = arguments != nullptr && i < argument_count &&
		                ReadGuestBytes(arguments[i], capture.bytes.data(), capture.bytes.size());
	}
	if (Enqueue(std::move(record))) {
		g_calls.fetch_add(1, std::memory_order_relaxed);
	}
}

void Event(std::string_view kind, std::string_view details) {
	if (!Active()) {
		return;
	}
	TraceRecord record;
	record.kind = RecordKind::Event;
	CopyText(kind, &record.function);
	CopyText(details, &record.details);
	if (Enqueue(std::move(record))) {
		g_events.fetch_add(1, std::memory_order_relaxed);
	}
}

} // namespace Common::CallTrace
