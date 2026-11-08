#include "common/pollCalls.h"

#include "common/virtualMemory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Common::PollCalls {
namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t OUT_CAPTURE_SIZE = 32;
constexpr size_t THUNK_PAGE_SIZE  = 4096;

struct OutCapture {
	bool                           valid = false;
	std::array<uint8_t, OUT_CAPTURE_SIZE> first {};
	std::array<uint8_t, OUT_CAPTURE_SIZE> last {};
};

struct ReturnCount {
	uint64_t value = 0;
	uint64_t count = 0;
};

struct CallEntry {
	uint64_t                  function_id = 0;
	uint64_t                  caller      = 0;
	uint64_t                  count       = 0;
	std::vector<ReturnCount>  returns;
	std::vector<OutCapture>   outputs;
};

struct ThreadRecord {
	std::mutex mutex;
	int guest_id = 0;
	std::string name;
	std::map<std::pair<uint64_t, uint64_t>, CallEntry> entries;
};

struct FunctionRecord {
	std::string          name;
	std::vector<uint8_t> output_arguments;
};

std::mutex                                 g_registry_mutex;
std::vector<std::shared_ptr<ThreadRecord>> g_threads;
thread_local std::shared_ptr<ThreadRecord> g_current_thread;
std::mutex                                 g_function_mutex;
std::vector<std::shared_ptr<FunctionRecord>> g_functions;
std::unordered_map<std::string, uint64_t>    g_function_ids;
std::string                                (*g_address_resolver)(uint64_t) = nullptr;
Clock::time_point                            g_start = Clock::now();
int64_t                                      g_at_ms = 60000;
int64_t                                      g_for_ms = 10000;
std::mutex                                   g_thunk_mutex;
std::vector<uint64_t>                        g_thunk_pages;
uint64_t                                     g_thunk_offset = 0;
std::atomic_uint64_t                         g_wrapped_functions {0};
std::atomic_uint64_t                         g_thunks {0};
std::atomic_uint64_t                         g_records {0};
std::atomic_uint64_t                         g_calls_seen {0};
std::atomic_uint64_t                         g_calls_dropped_no_thread {0};

std::vector<uint8_t> OutputArguments(std::string_view name) {
	if (name.ends_with("SystemServiceGetStatus") || name.ends_with("SystemServiceReceiveEvent") ||
	    name.ends_with("SystemServiceGetDisplaySafeAreaInfo") || name.ends_with("UserServiceGetInitialUser") ||
	    name.ends_with("UserServiceGetEvent") || name.ends_with("UserServiceGetLoginUserIdList") ||
	    name.ends_with("NpTrophy2CreateContext") || name.ends_with("NpTrophy2CreateHandle")) {
		return {0};
	}
	if (name.ends_with("SystemServiceParamGetInt") || name.ends_with("AppContentAppParamGetInt") ||
	    name.ends_with("NpGetState") || name.ends_with("NpGetNpId") ||
	    name.ends_with("NpEntitlementAccessInitialize") || name.ends_with("AppContentInitialize") ||
	    name.ends_with("SaveDataGetEventResult") || name.ends_with("SaveDataMount3") ||
	    name.ends_with("SaveDataTransferringMount") || name.ends_with("PadRead") ||
	    name.ends_with("PadReadState") || name.ends_with("PadGetControllerInformation") ||
	    name.ends_with("VideoOutGetResolutionStatus") || name.ends_with("VideoOutGetFlipStatus")) {
		return {1};
	}
	if (name.ends_with("AppContentAddcontMount") || name.ends_with("NpEntitlementAccessGetEntitlementKey")) {
		return {2};
	}
	if (name.ends_with("NpEntitlementAccessGetAddcontEntitlementInfoList")) {
		return {1, 3};
	}
	if (name.ends_with("NpEntitlementAccessGetAddcontEntitlementInfo")) {
		return {2};
	}
	return {};
}

uint64_t FunctionId(const std::string& name) {
	std::lock_guard lock(g_function_mutex);
	if (const auto it = g_function_ids.find(name); it != g_function_ids.end()) {
		return it->second;
	}
	const uint64_t id = g_functions.size();
	g_functions.push_back(std::make_shared<FunctionRecord>(FunctionRecord {name, OutputArguments(name)}));
	g_function_ids.emplace(name, id);
	return id;
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

bool WindowOpen() {
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_start).count();
	return elapsed >= g_at_ms && elapsed < g_at_ms + g_for_ms;
}

void LogWindowOpen() {
	size_t threads = 0;
	{
		std::lock_guard lock(g_registry_mutex);
		threads = g_threads.size();
	}
	std::fprintf(stderr, "POLL_CALLS window_open threads_registered=%zu\n", threads);
	std::fflush(stderr);
}

void LogWindowClose() {
	std::fprintf(stderr, "POLL_CALLS window_close records=%llu calls_seen=%llu calls_dropped_no_thread=%llu\n",
	             static_cast<unsigned long long>(g_records.load(std::memory_order_relaxed)),
	             static_cast<unsigned long long>(g_calls_seen.load(std::memory_order_relaxed)),
	             static_cast<unsigned long long>(g_calls_dropped_no_thread.load(std::memory_order_relaxed)));
	std::fflush(stderr);
}

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
	return g_address_resolver != nullptr ? g_address_resolver(caller) : Hex(caller);
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

void Dump() {
	struct ThreadSnapshot {
		int guest_id = 0;
		std::string name;
		std::shared_ptr<ThreadRecord> record;
	};
	std::vector<ThreadSnapshot> threads;
	std::vector<std::shared_ptr<FunctionRecord>> functions;
	{
		std::lock_guard lock(g_registry_mutex);
		threads.reserve(g_threads.size());
		for (const auto& record: g_threads) {
			std::lock_guard record_lock(record->mutex);
			threads.push_back(ThreadSnapshot {record->guest_id, record->name, record});
		}
	}
	{
		std::lock_guard lock(g_function_mutex);
		functions = g_functions;
	}
	std::ranges::sort(threads, [](const auto& a, const auto& b) {
		if (a.name != b.name) {
			return a.name < b.name;
		}
		return a.guest_id < b.guest_id;
	});

	std::string output = "POLL_CALL_DUMP at_ms=" + std::to_string(g_at_ms) +
	                     " for_ms=" + std::to_string(g_for_ms) + "\n";
	for (const auto& thread: threads) {
		std::vector<CallEntry> entries;
		{
			std::lock_guard lock(thread.record->mutex);
			entries.reserve(thread.record->entries.size());
			for (const auto& [key, entry]: thread.record->entries) {
				entries.push_back(entry);
			}
		}
		std::ranges::sort(entries, [](const CallEntry& a, const CallEntry& b) {
			if (a.count != b.count) {
				return a.count > b.count;
			}
			if (a.function_id != b.function_id) {
				return a.function_id < b.function_id;
			}
			return a.caller < b.caller;
		});
		for (const auto& entry: entries) {
			const auto& function = functions[entry.function_id];
			output += "POLL_CALL thread=" + CleanField(thread.name) + " tid=" + std::to_string(thread.guest_id) +
			          " fn=" + CleanField(function->name) + " caller=" + CleanField(CallerText(entry.caller)) +
			          " count=" + std::to_string(entry.count) + " rets=[";
			for (size_t i = 0; i < entry.returns.size(); i++) {
				if (i != 0) {
					output += ',';
				}
				output += Hex(entry.returns[i].value) + "x" + std::to_string(entry.returns[i].count);
			}
			output += ']';
			for (size_t i = 0; i < entry.outputs.size(); i++) {
				const auto& capture = entry.outputs[i];
				output += " out" + std::to_string(function->output_arguments[i]) + ".first=" +
				          (capture.valid ? BytesText(capture.first) : "unavailable") + " out" +
				          std::to_string(function->output_arguments[i]) + ".last=" +
				          (capture.valid ? BytesText(capture.last) : "unavailable");
			}
			output += '\n';
		}
	}
	std::fwrite(output.data(), 1, output.size(), stderr);
	std::fflush(stderr);
}

void Emit(std::vector<uint8_t>* code, std::initializer_list<uint8_t> bytes) {
	code->insert(code->end(), bytes);
}

void Emit32(std::vector<uint8_t>* code, uint32_t value) {
	for (size_t i = 0; i < sizeof(value); i++) {
		code->push_back(static_cast<uint8_t>(value >> (i * 8)));
	}
}

void Emit64(std::vector<uint8_t>* code, uint64_t value) {
	for (size_t i = 0; i < sizeof(value); i++) {
		code->push_back(static_cast<uint8_t>(value >> (i * 8)));
	}
}

std::vector<uint8_t> MakeThunk(uint64_t target, uint64_t function_id) {
	constexpr uint32_t stack_size = 216;
	std::vector<uint8_t> code;
	code.reserve(192);
	Emit(&code, {0x48, 0x81, 0xec});
	Emit32(&code, stack_size);
	Emit(&code, {0x48, 0x89, 0x7c, 0x24, 0x40});
	Emit(&code, {0x48, 0x89, 0x74, 0x24, 0x48});
	Emit(&code, {0x48, 0x89, 0x54, 0x24, 0x50});
	Emit(&code, {0x48, 0x89, 0x4c, 0x24, 0x58});
	Emit(&code, {0x4c, 0x89, 0x44, 0x24, 0x60});
	Emit(&code, {0x4c, 0x89, 0x4c, 0x24, 0x68});
	Emit(&code, {0x48, 0xb8});
	Emit64(&code, target);
	Emit(&code, {0xff, 0xd0});
	Emit(&code, {0x48, 0x89, 0x04, 0x24});
	Emit(&code, {0x48, 0x89, 0x54, 0x24, 0x08});
	Emit(&code, {0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x10});
	Emit(&code, {0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x20});
	Emit(&code, {0x48, 0xbf});
	Emit64(&code, function_id);
	Emit(&code, {0x48, 0x8b, 0xb4, 0x24});
	Emit32(&code, stack_size);
	Emit(&code, {0x48, 0x8b, 0x14, 0x24});
	Emit(&code, {0x48, 0x8d, 0x4c, 0x24, 0x40});
	Emit(&code, {0x48, 0xb8});
	Emit64(&code, reinterpret_cast<uint64_t>(&RecordCall));
	Emit(&code, {0xff, 0xd0});
	Emit(&code, {0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x10});
	Emit(&code, {0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x20});
	Emit(&code, {0x48, 0x8b, 0x54, 0x24, 0x08});
	Emit(&code, {0x48, 0x8b, 0x04, 0x24});
	Emit(&code, {0x48, 0x81, 0xc4});
	Emit32(&code, stack_size);
	Emit(&code, {0xc3});
	return code;
}

} // namespace

bool Enabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_DIAG_POLL_CALLS");
		return value != nullptr && value[0] == '1';
	}();
	return enabled;
}

void Initialize() {
	if (!Enabled()) {
		return;
	}
	g_start = Clock::now();
	g_at_ms = [] {
		const char* value = std::getenv("KYTY_DIAG_POLL_CALLS_AT");
		const double seconds = value != nullptr ? std::strtod(value, nullptr) : 60.0;
		return std::isfinite(seconds) && seconds >= 0 ? static_cast<int64_t>(seconds * 1000.0) : int64_t {60000};
	}();
	g_for_ms = [] {
		const char* value = std::getenv("KYTY_DIAG_POLL_CALLS_FOR");
		const double seconds = value != nullptr ? std::strtod(value, nullptr) : 10.0;
		return std::isfinite(seconds) && seconds >= 0 ? static_cast<int64_t>(seconds * 1000.0) : int64_t {10000};
	}();
	std::fprintf(stderr, "POLL_CALLS wrapped=%llu thunks=%llu enabled=1\n",
	             static_cast<unsigned long long>(g_wrapped_functions.load(std::memory_order_relaxed)),
	             static_cast<unsigned long long>(g_thunks.load(std::memory_order_relaxed)));
	std::fflush(stderr);
	std::thread([] {
		std::this_thread::sleep_until(g_start + std::chrono::milliseconds(g_at_ms));
		LogWindowOpen();
		std::this_thread::sleep_until(g_start + std::chrono::milliseconds(g_at_ms + g_for_ms));
		LogWindowClose();
		Dump();
	}).detach();
}

void RegisterWrapped() {
	if (Enabled()) {
		g_wrapped_functions.fetch_add(1, std::memory_order_relaxed);
	}
}

void SetAddressResolver(std::string (*resolver)(uint64_t address)) {
	g_address_resolver = resolver;
}

void RegisterCurrentThread(int guest_id, const char* name) {
	if (!Enabled()) {
		return;
	}
	auto record = std::make_shared<ThreadRecord>();
	record->guest_id = guest_id;
	record->name = name != nullptr ? name : "";
	g_current_thread = record;
	std::lock_guard lock(g_registry_mutex);
	g_threads.push_back(std::move(record));
}

void UnregisterCurrentThread() {
	g_current_thread.reset();
}

void SetCurrentThreadName(const char* name) {
	if (g_current_thread != nullptr) {
		std::lock_guard lock(g_current_thread->mutex);
		g_current_thread->name = name != nullptr ? name : "";
	}
}

uint64_t Wrap(uint64_t target, const char* library, const char* function) {
#if !defined(__x86_64__) && !defined(_M_X64)
	return target;
#else
	if (!Enabled() || target == 0) {
		return target;
	}
	const std::string name = std::string(library != nullptr ? library : "unknown") + "::" +
	                         (function != nullptr ? function : "unknown");
	const uint64_t function_id = FunctionId(name);
	const auto code = MakeThunk(target, function_id);
	std::lock_guard lock(g_thunk_mutex);
	if (g_thunk_pages.empty() || g_thunk_offset + code.size() > THUNK_PAGE_SIZE) {
		const auto page = VirtualMemory::Alloc(0, THUNK_PAGE_SIZE, VirtualMemory::Mode::ExecuteReadWrite);
		if (page == 0) {
			return target;
		}
		g_thunk_pages.push_back(page);
		g_thunk_offset = 0;
	}
	const auto address = g_thunk_pages.back() + g_thunk_offset;
	std::memcpy(reinterpret_cast<void*>(address), code.data(), code.size());
	VirtualMemory::FlushInstructionCache(address, code.size());
	g_thunk_offset += code.size();
	g_wrapped_functions.fetch_add(1, std::memory_order_relaxed);
	g_thunks.fetch_add(1, std::memory_order_relaxed);
	return address;
#endif
}

void RecordResolved(const char* library, const char* module, const char* function, uint64_t caller,
                    uint64_t result, const uint64_t* arguments) {
	if (!Enabled() || !WindowOpen()) {
		return;
	}
	const std::string name = std::string(library != nullptr ? library : "unknown") + "::" +
	                         (module != nullptr ? module : "unknown") + "::" +
	                         (function != nullptr ? function : "unknown");
	RecordCall(FunctionId(name), caller, result, arguments);
}

void KYTY_SYSV_ABI RecordCall(uint64_t function_id, uint64_t caller, uint64_t result,
                              const uint64_t* arguments) {
	if (!Enabled() || !WindowOpen()) {
		return;
	}
	g_calls_seen.fetch_add(1, std::memory_order_relaxed);
	if (g_current_thread == nullptr) {
		g_calls_dropped_no_thread.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	std::shared_ptr<FunctionRecord> function;
	{
		std::lock_guard lock(g_function_mutex);
		if (function_id >= g_functions.size()) {
			return;
		}
		function = g_functions[function_id];
	}
	auto& thread = *g_current_thread;
	std::lock_guard lock(thread.mutex);
	auto& entry = thread.entries[{function_id, caller}];
	if (entry.count == 0) {
		entry.function_id = function_id;
		entry.caller = caller;
		entry.outputs.resize(function->output_arguments.size());
		g_records.fetch_add(1, std::memory_order_relaxed);
	}
	entry.count++;
	bool found = false;
	for (auto& return_count: entry.returns) {
		if (return_count.value == result) {
			return_count.count++;
			found = true;
			break;
		}
	}
	if (!found && entry.returns.size() < 4) {
		entry.returns.push_back(ReturnCount {result, 1});
	}
	if (arguments == nullptr) {
		return;
	}
	for (size_t i = 0; i < function->output_arguments.size(); i++) {
		auto& capture = entry.outputs[i];
		std::array<uint8_t, OUT_CAPTURE_SIZE> bytes {};
		if (!ReadGuestBytes(arguments[function->output_arguments[i]], bytes.data(), bytes.size())) {
			continue;
		}
		if (!capture.valid) {
			capture.first = bytes;
			capture.valid = true;
		}
		capture.last = bytes;
	}
}

void RecordUnresolved(const char* function, uint64_t caller) {
	if (!Enabled() || !WindowOpen()) {
		return;
	}
	RecordCall(FunctionId(function != nullptr ? function : "unknown"), caller, 0, nullptr);
}

} // namespace Common::PollCalls
