#include "common/guestWaits.h"

#include "common/pollCalls.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Common::GuestWaits {
namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t CALL_HISTORY_SIZE = 32;

struct ThreadRecord {
	std::mutex                          mutex;
	int                                 guest_id  = 0;
	int                                 host_id   = 0;
	uint64_t                            system_id = 0;
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

struct GpuWriteActivity {
	uint64_t    count = 0;
	uint64_t    last_value = 0;
	uint64_t    size = 0;
	uint32_t    queue = 0;
	std::string packet;
};

struct ActivityData {
	uint64_t                       file_opens = 0;
	uint64_t                       file_reads = 0;
	uint64_t                       file_read_bytes = 0;
	uint64_t                       ampr_submitted = 0;
	uint64_t                       ampr_completed = 0;
	std::string                    last_file_path;
	std::map<int16_t, uint64_t>    equeue_events;
	std::map<uint64_t, GpuWriteActivity> gpu_writes;
};

struct ActivityState {
	std::mutex  mutex;
	ActivityData data;
};

std::mutex                                      g_registry_mutex;
std::vector<std::shared_ptr<ThreadRecord>>      g_records;
thread_local std::shared_ptr<ThreadRecord>      g_current_record;
std::once_flag                                  g_worker_once;
std::string                                   (*g_address_resolver)(uint64_t) = nullptr;
Clock::time_point                               g_start;
int64_t                                         g_once_at_ms = -1;
bool                                            g_periodic = false;
bool                                            g_cpu_sample = false;
ActivityState                                   g_activity;
bool                                            g_guest_threads = false;
int64_t                                         g_guest_threads_period_ms = 1000;
#if defined(_WIN32)
std::once_flag                                  g_cpu_worker_once;
std::vector<std::string>                        g_cpu_sample_threads;
int64_t                                         g_cpu_sample_once_at_ms = -1;
#endif

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

void MixSignature(uint64_t& hash, uint64_t value) {
	hash ^= value;
	hash *= 1099511628211ull;
}

void MixSignature(uint64_t& hash, const std::string& value) {
	for (const unsigned char c: value) MixSignature(hash, c);
}

uint64_t DumpGuestThreads() {
	std::vector<std::shared_ptr<ThreadRecord>> records;
	{
		std::lock_guard lock(g_registry_mutex);
		records = g_records;
	}
	ActivityData activity;
	{
		std::lock_guard lock(g_activity.mutex);
		activity = g_activity.data;
	}

	const auto now = Clock::now();
	std::fprintf(stderr, "GUEST_THREADS_DUMP elapsed_ms=%lld\n",
	             static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start).count()));
	Dump(4);
	uint64_t equeue_total = 0;
	for (const auto& [filter, count]: activity.equeue_events) equeue_total += count;
	const uint64_t pending = activity.ampr_submitted >= activity.ampr_completed
	                             ? activity.ampr_submitted - activity.ampr_completed
	                             : 0;
	std::string output = "GUEST_THREADS_ACTIVITY file_opens=" + std::to_string(activity.file_opens) +
	                     " file_reads=" + std::to_string(activity.file_reads) +
	                     " file_read_bytes=" + std::to_string(activity.file_read_bytes) +
	                     " last_file=" + CleanField(activity.last_file_path) +
	                     " ampr_submitted=" + std::to_string(activity.ampr_submitted) +
	                     " ampr_completed=" + std::to_string(activity.ampr_completed) +
	                     " ampr_pending=" + std::to_string(pending) +
	                     " equeue_events=" + std::to_string(equeue_total) +
	                     " gpu_addresses=" + std::to_string(activity.gpu_writes.size()) + "\n";
	for (const auto& [filter, count]: activity.equeue_events) {
		output += "GUEST_THREADS_EQUEUE filter=" + std::to_string(filter) + " count=" + std::to_string(count) + "\n";
	}
	size_t gpu_index = 0;
	for (const auto& [address, write]: activity.gpu_writes) {
		if (gpu_index++ >= 64u) break;
		output += "GUEST_THREADS_GPU addr=" + Hex(address) + " count=" + std::to_string(write.count) +
		          " last_value=" + Hex(write.last_value) + " size=" + std::to_string(write.size) +
		          " queue=" + std::to_string(write.queue) + " packet=" + CleanField(write.packet) + "\n";
	}
	std::fwrite(output.data(), 1, output.size(), stderr);
	std::fflush(stderr);

	uint64_t hash = 14695981039346656037ull;
	MixSignature(hash, activity.file_opens);
	MixSignature(hash, activity.file_reads);
	MixSignature(hash, activity.file_read_bytes);
	MixSignature(hash, activity.last_file_path);
	MixSignature(hash, activity.ampr_submitted);
	MixSignature(hash, activity.ampr_completed);
	for (const auto& [filter, count]: activity.equeue_events) {
		MixSignature(hash, static_cast<uint16_t>(filter));
		MixSignature(hash, count);
	}
	for (const auto& [address, write]: activity.gpu_writes) {
		MixSignature(hash, address);
		MixSignature(hash, write.count);
		MixSignature(hash, write.last_value);
		MixSignature(hash, write.size);
		MixSignature(hash, write.queue);
		MixSignature(hash, write.packet);
	}
	for (const auto& record: records) {
		std::lock_guard lock(record->mutex);
		MixSignature(hash, static_cast<uint32_t>(record->guest_id));
		MixSignature(hash, record->blocked ? 1u : 0u);
		MixSignature(hash, record->kind);
		MixSignature(hash, record->object_name);
		MixSignature(hash, record->object);
		MixSignature(hash, record->caller);
		MixSignature(hash, record->details);
		MixSignature(hash, record->last_caller);
		MixSignature(hash, record->next_call);
	}
	return hash;
}

void EnsureWorker() {
	std::call_once(g_worker_once, [] {
		std::thread([] {
			bool once_done = false;
			auto next_periodic = g_start + std::chrono::seconds(20);
			auto next_threads = g_start + std::chrono::milliseconds(g_guest_threads_period_ms);
			uint64_t previous_signature = 0;
			uint32_t unchanged_blocks = 0;
			while (g_periodic || g_guest_threads || !once_done) {
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
				if (g_guest_threads && now >= next_threads) {
					const auto signature = DumpGuestThreads();
					unchanged_blocks = signature == previous_signature ? unchanged_blocks + 1u : 1u;
					if (unchanged_blocks == 3u) {
						std::fputs("GUEST_THREADS_IDLE unchanged_blocks=3\n", stderr);
						std::fflush(stderr);
					}
					previous_signature = signature;
					next_threads = now + std::chrono::milliseconds(g_guest_threads_period_ms);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}).detach();
	});
}

#if defined(_WIN32)
struct CpuRegisters {
	uint64_t rax = 0;
	uint64_t rbx = 0;
	uint64_t rcx = 0;
	uint64_t rdx = 0;
	uint64_t rsi = 0;
	uint64_t rdi = 0;
	uint64_t r8 = 0;
	uint64_t r9 = 0;
	uint64_t r10 = 0;
	uint64_t r11 = 0;
	uint64_t r12 = 0;
	uint64_t r13 = 0;
	uint64_t r14 = 0;
	uint64_t r15 = 0;
	uint64_t rsp = 0;
	uint64_t rbp = 0;
};

struct CpuSampleSnapshot {
	uint64_t     sequence = 0;
	int          guest_id = 0;
	CpuRegisters registers;
};

struct CpuSite {
	uint64_t                          count = 0;
	std::array<CpuSampleSnapshot, 3> samples {};
	size_t                            next_sample = 0;
	size_t                            sample_count = 0;
};

std::unordered_map<uint64_t, CpuSite> g_cpu_sites;
uint64_t                              g_cpu_sample_count = 0;
uint64_t                              g_cpu_sample_sequence = 0;

bool CpuThreadSelected(const std::string& name) {
	return std::find(g_cpu_sample_threads.begin(), g_cpu_sample_threads.end(), name) != g_cpu_sample_threads.end();
}

bool EmulatorAddress(uint64_t address) {
	HMODULE owner = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCWSTR>(address), &owner)) {
		return false;
	}
	return owner == GetModuleHandleW(nullptr);
}

uint64_t CpuSampleSite(uint64_t rip, const std::shared_ptr<ThreadRecord>& record) {
	if (g_address_resolver != nullptr && g_address_resolver(rip).rfind("unknown+", 0) != 0) {
		return rip;
	}
	if (!EmulatorAddress(rip)) {
		return 0;
	}
	std::lock_guard lock(record->mutex);
	return record->last_caller;
}

CpuRegisters GetCpuRegisters(const CONTEXT& context) {
	return CpuRegisters {
	    .rax = context.Rax,
	    .rbx = context.Rbx,
	    .rcx = context.Rcx,
	    .rdx = context.Rdx,
	    .rsi = context.Rsi,
	    .rdi = context.Rdi,
	    .r8 = context.R8,
	    .r9 = context.R9,
	    .r10 = context.R10,
	    .r11 = context.R11,
	    .r12 = context.R12,
	    .r13 = context.R13,
	    .r14 = context.R14,
	    .r15 = context.R15,
	    .rsp = context.Rsp,
	    .rbp = context.Rbp,
	};
}

void SampleCpuThread(const std::shared_ptr<ThreadRecord>& record) {
	int guest_id = 0;
	{
		std::lock_guard lock(record->mutex);
		if (!CpuThreadSelected(record->name)) {
			return;
		}
		guest_id = record->guest_id;
	}
	const auto system_id = static_cast<DWORD>(record->system_id);
	if (system_id == 0) {
		return;
	}
	HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
	                           system_id);
	if (thread == nullptr) {
		return;
	}
	CONTEXT context {};
	uint64_t rip = 0;
	if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
		context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		if (GetThreadContext(thread, &context)) {
			rip = context.Rip;
		}
		ResumeThread(thread);
	}
	CloseHandle(thread);
	if (rip == 0) {
		return;
	}
	const uint64_t site_address = CpuSampleSite(rip, record);
	if (site_address == 0) {
		return;
	}

	g_cpu_sample_count++;
	auto& site = g_cpu_sites[site_address];
	site.count++;
	auto& snapshot = site.samples[site.next_sample];
	snapshot.sequence = ++g_cpu_sample_sequence;
	snapshot.guest_id = guest_id;
	snapshot.registers = GetCpuRegisters(context);
	site.next_sample = (site.next_sample + 1) % site.samples.size();
	site.sample_count = std::min(site.sample_count + 1, site.samples.size());
}

void DumpCpuSamples(bool detailed) {
	std::vector<std::pair<uint64_t, const CpuSite*>> sorted;
	sorted.reserve(g_cpu_sites.size());
	for (const auto& [address, site]: g_cpu_sites) {
		sorted.emplace_back(address, &site);
	}
	std::ranges::sort(sorted, [](const auto& a, const auto& b) {
		if (a.second->count != b.second->count) {
			return a.second->count > b.second->count;
		}
		return a.first < b.first;
	});

	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_start).count();
	const size_t site_limit = detailed ? 50 : 20;
	const size_t detail_limit = detailed ? 10 : 5;
	const size_t register_samples = detailed ? 3 : 1;
	std::string output = "CPU_SAMPLE_DUMP elapsed_ms=" + std::to_string(elapsed) +
	                     " mode=" + (detailed ? "once" : "periodic") +
	                     " samples=" + std::to_string(g_cpu_sample_count) + " sites=" + std::to_string(sorted.size()) +
	                     "\n";
	for (size_t i = 0; i < sorted.size() && i < site_limit; i++) {
		const auto& [address, site] = sorted[i];
		const std::string site_name = CallerText(address);
		output += "CPU_SAMPLE_SITE rank=" + std::to_string(i + 1) + " site=" + site_name + " rip=" + Hex(address) +
		          " n=" + std::to_string(site->count) + "\n";
		if (i >= detail_limit) {
			continue;
		}

		const size_t code_size = detailed ? 64 : 32;
		const uint64_t code_address = address - code_size / 2;
		std::array<uint8_t, 64> code {};
		SIZE_T code_read = 0;
		ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(code_address), code.data(), code_size,
		                  &code_read);
		output += "CPU_SAMPLE_CODE site=" + site_name + " base=" + Hex(code_address) + " bytes=";
		for (size_t byte = 0; byte < code_size; byte++) {
			char hex[3] {};
			if (byte < code_read) {
				std::snprintf(hex, sizeof(hex), "%02x", code[byte]);
				output += hex;
			} else {
				output += "??";
			}
		}
		output += "\n";

		const size_t count = std::min(site->sample_count, register_samples);
		for (size_t sample = 0; sample < count; sample++) {
			const size_t index = (site->next_sample + site->samples.size() - count + sample) % site->samples.size();
			const auto& snapshot = site->samples[index];
			const auto& r = snapshot.registers;
			char registers[768] {};
			std::snprintf(registers, sizeof(registers),
			              "RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx RSI=0x%llx RDI=0x%llx "
			              "R8=0x%llx R9=0x%llx R10=0x%llx R11=0x%llx R12=0x%llx R13=0x%llx "
			              "R14=0x%llx R15=0x%llx RSP=0x%llx RBP=0x%llx",
			              static_cast<unsigned long long>(r.rax), static_cast<unsigned long long>(r.rbx),
			              static_cast<unsigned long long>(r.rcx), static_cast<unsigned long long>(r.rdx),
			              static_cast<unsigned long long>(r.rsi), static_cast<unsigned long long>(r.rdi),
			              static_cast<unsigned long long>(r.r8), static_cast<unsigned long long>(r.r9),
			              static_cast<unsigned long long>(r.r10), static_cast<unsigned long long>(r.r11),
			              static_cast<unsigned long long>(r.r12), static_cast<unsigned long long>(r.r13),
			              static_cast<unsigned long long>(r.r14), static_cast<unsigned long long>(r.r15),
			              static_cast<unsigned long long>(r.rsp), static_cast<unsigned long long>(r.rbp));
			output += "CPU_SAMPLE_REGS site=" + site_name + " guest_tid=" + std::to_string(snapshot.guest_id) +
			          " sequence=" + std::to_string(snapshot.sequence) + " " + registers + "\n";
		}
	}
	std::fwrite(output.data(), 1, output.size(), stderr);
	std::fflush(stderr);
	g_cpu_sites.clear();
	g_cpu_sample_count = 0;
}

void EnsureCpuSampler() {
	std::call_once(g_cpu_worker_once, [] {
		std::thread([] {
			SetThreadDescription(GetCurrentThread(), L"guest_cpu_sampler");
			bool once_done = g_cpu_sample_once_at_ms < 0;
			auto next_periodic = g_start + std::chrono::seconds(20);
			for (;;) {
				const auto now = Clock::now();
				if (!once_done && now - g_start >= std::chrono::milliseconds(g_cpu_sample_once_at_ms)) {
					DumpCpuSamples(true);
					once_done = true;
					next_periodic = now + std::chrono::seconds(20);
				} else if (now >= next_periodic) {
					DumpCpuSamples(false);
					next_periodic = now + std::chrono::seconds(20);
				}
				std::vector<std::shared_ptr<ThreadRecord>> records;
				{
					std::lock_guard lock(g_registry_mutex);
					records = g_records;
				}
				for (const auto& record: records) {
					SampleCpuThread(record);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}).detach();
	});
}
#endif

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
#if defined(_WIN32)
	g_cpu_sample = [] {
		const char* value = std::getenv("KYTY_DIAG_CPU_SAMPLE");
		return value != nullptr && value[0] == '1';
	}();
	if (g_cpu_sample) {
		const char* names = std::getenv("KYTY_DIAG_CPU_SAMPLE_THREADS");
		const std::string list = names != nullptr && names[0] != '\0' ? names : "MainThread";
		size_t begin = 0;
		while (begin <= list.size()) {
			const size_t end = list.find(',', begin);
			std::string name = list.substr(begin, end == std::string::npos ? end : end - begin);
			const size_t first = name.find_first_not_of(" \t");
			const size_t last = name.find_last_not_of(" \t");
			if (first != std::string::npos) {
				g_cpu_sample_threads.push_back(name.substr(first, last - first + 1));
			}
			if (end == std::string::npos) {
				break;
			}
			begin = end + 1;
		}
		const char* value = std::getenv("KYTY_DIAG_CPU_SAMPLE_AT");
		if (value != nullptr && value[0] != '\0') {
			const double seconds = std::strtod(value, nullptr);
			g_cpu_sample_once_at_ms =
			    std::isfinite(seconds) && seconds >= 0 ? static_cast<int64_t>(seconds * 1000.0) : int64_t {-1};
		}
	}
#endif
	g_guest_threads_period_ms = [] {
		const char* value = std::getenv("KYTY_DIAG_GUEST_THREADS");
		if (value == nullptr || value[0] == '\0') return int64_t {0};
		const double seconds = std::strtod(value, nullptr);
		return std::isfinite(seconds) && seconds > 0
		           ? std::max<int64_t>(1, static_cast<int64_t>(seconds * 1000.0))
		           : int64_t {0};
	}();
	g_guest_threads = g_guest_threads_period_ms > 0;
	g_start = Clock::now();
	PollCalls::Initialize();
	EnabledFlag().store(g_periodic || g_once_at_ms >= 0 || g_cpu_sample || g_guest_threads || PollCalls::Enabled(),
	                    std::memory_order_release);
}

bool GuestThreadsEnabled() {
	return g_guest_threads;
}

void RecordFileOpen(const char* guest_path) {
	if (!GuestThreadsEnabled()) return;
	std::lock_guard lock(g_activity.mutex);
	g_activity.data.file_opens++;
	g_activity.data.last_file_path = guest_path != nullptr ? guest_path : "";
}

void RecordFileRead(const char* guest_path, uint64_t bytes) {
	if (!GuestThreadsEnabled()) return;
	std::lock_guard lock(g_activity.mutex);
	g_activity.data.file_reads++;
	g_activity.data.file_read_bytes += bytes;
	g_activity.data.last_file_path = guest_path != nullptr ? guest_path : "";
}

void RecordAmprSubmitted() {
	if (!GuestThreadsEnabled()) return;
	std::lock_guard lock(g_activity.mutex);
	g_activity.data.ampr_submitted++;
}

void RecordAmprCompleted() {
	if (!GuestThreadsEnabled()) return;
	std::lock_guard lock(g_activity.mutex);
	g_activity.data.ampr_completed++;
}

void RecordEqueueEvent(int16_t filter) {
	if (!GuestThreadsEnabled()) return;
	std::lock_guard lock(g_activity.mutex);
	g_activity.data.equeue_events[filter]++;
}

void RecordGpuWrite(const char* packet, uint32_t queue, uint64_t address, uint64_t value, uint64_t size) {
	if (!GuestThreadsEnabled() || address == 0 || size == 0) return;
	std::lock_guard lock(g_activity.mutex);
	auto& write = g_activity.data.gpu_writes[address];
	write.count++;
	write.last_value = value;
	write.size = size;
	write.queue = queue;
	write.packet = packet != nullptr ? packet : "";
}

void SetAddressResolver(std::string (*resolver)(uint64_t address)) {
	g_address_resolver = resolver;
	PollCalls::SetAddressResolver(resolver);
}

void RegisterCurrentThread(int guest_id, int host_id, const char* name, uint64_t system_id) {
	if (!Enabled()) {
		return;
	}
	PollCalls::RegisterCurrentThread(guest_id, name);
	auto record = std::make_shared<ThreadRecord>();
	record->guest_id = guest_id;
	record->host_id = host_id;
	record->system_id = system_id;
	record->name = name != nullptr ? name : "";
	g_current_record = record;
	{
		std::lock_guard lock(g_registry_mutex);
		g_records.push_back(record);
	}
	if (g_periodic || g_once_at_ms >= 0 || g_guest_threads) {
		EnsureWorker();
	}
#if defined(_WIN32)
	if (g_cpu_sample) {
		EnsureCpuSampler();
	}
#endif
}

void UnregisterCurrentThread() {
	if (g_current_record == nullptr) {
		return;
	}
	PollCalls::UnregisterCurrentThread();
	std::lock_guard lock(g_registry_mutex);
	std::erase(g_records, g_current_record);
	g_current_record.reset();
}

void SetCurrentThreadName(const char* name) {
	if (g_current_record == nullptr) {
		return;
	}
	PollCalls::SetCurrentThreadName(name);
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
