#include "kernel/hangProbe.h"

#include "common/common.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Kernel::HangProbe {

namespace {

using Clock = std::chrono::steady_clock;

struct GuestThread {
	std::string name;
	uint64_t    host_thread_id = 0;
	uint64_t    stack_low = 0, stack_high = 0;
};

struct Module {
	std::string name;
	uint64_t    base = 0, size = 0;
};

struct Wait {
	const void*       address = nullptr;
	uint64_t          reference = 0, mask = 0;
	uint32_t          function = 0;
	const char*       kind = "";
	Clock::time_point since;
	bool              reported = false;
};

struct State {
	std::mutex                  mutex;
	std::map<int, GuestThread>  threads;
	std::vector<Module>         modules;
	std::map<int, Wait>         waits; // by queue
	std::FILE*                  file = nullptr;
	std::once_flag              started;
};

State& Get() {
	static State state;
	return state;
}

int Seconds() {
	static const int seconds = [] {
		const char* value = std::getenv("KYTY_HANG_PROBE");
		if (value == nullptr) return 0;
		const int parsed = std::atoi(value);
		return parsed > 0 ? parsed : 0;
	}();
	return seconds;
}

// Under the state's mutex.
void Write(State& state, const char* text) {
	if (state.file == nullptr) {
		state.file = std::fopen("hang-probe.txt", "w");
		if (state.file == nullptr) return;
	}
	std::fputs(text, state.file);
	std::fflush(state.file);
}

template <typename... Args>
void Print(State& state, const char* format, Args... args) {
	char text[1024];
	std::snprintf(text, sizeof(text), format, args...);
	Write(state, text);
}

// "eboot.bin+0x123456", or empty outside every guest module.
[[maybe_unused]] std::string Where(const State& state, uint64_t address) {
	for (const auto& module: state.modules) {
		if (address >= module.base && address < module.base + module.size) {
			char text[256];
			std::snprintf(text, sizeof(text), "%s+0x%" PRIx64, module.name.c_str(), address - module.base);
			return text;
		}
	}
	return {};
}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
std::string HostWhere(uint64_t address) {
	HMODULE module = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCSTR>(address), &module) == 0) {
		return "?";
	}
	char path[MAX_PATH] {};
	GetModuleFileNameA(module, path, MAX_PATH);
	const char* name = std::strrchr(path, '\\');
	char        text[300];
	std::snprintf(text, sizeof(text), "%s+0x%" PRIx64, name != nullptr ? name + 1 : path,
	              address - reinterpret_cast<uint64_t>(module));
	return text;
}

bool Readable(uint64_t address, size_t bytes) {
	MEMORY_BASIC_INFORMATION info {};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) return false;
	if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
	return address + bytes <= reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize;
}

struct Sample {
	uint64_t rip = 0, rsp = 0, rbp = 0;
	bool     ok  = false;
};

Sample Take(uint64_t host_thread_id) {
	Sample sample;
	HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
	                           static_cast<DWORD>(host_thread_id));
	if (handle == nullptr) return sample;
	if (SuspendThread(handle) != static_cast<DWORD>(-1)) {
		CONTEXT context {};
		context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		if (GetThreadContext(handle, &context) != 0) {
			sample.rip = context.Rip;
			sample.rsp = context.Rsp;
			sample.rbp = context.Rbp;
			sample.ok  = true;
		}
		ResumeThread(handle);
	}
	CloseHandle(handle);
	return sample;
}

// Every guest thread, sampled. The threads are sampled without the state's mutex (a suspended thread
// may hold it, or the heap's lock: nothing between SuspendThread and ResumeThread allocates).
void Dump(int round) {
	std::map<int, GuestThread> threads;
	{
		auto&            state = Get();
		std::scoped_lock lock(state.mutex);
		threads = state.threads;
	}
	struct Result {
		int                 id = 0;
		GuestThread         thread;
		std::vector<Sample> samples;
		std::vector<uint8_t> code;  // rip-48..rip+48 of the last sample, when it is the game's
		std::vector<uint64_t> stack; // return addresses into the game, the newest first
	};
	std::vector<Result> results;
	results.reserve(threads.size());
	// (The module list does not change once the game runs.)
	State* state = &Get();
	for (const auto& [id, thread]: threads) {
		Result result {id, thread, {}, {}, {}};
		result.samples.reserve(8);
		for (int i = 0; i < 8; i++) {
			result.samples.push_back(Take(thread.host_thread_id));
			std::this_thread::sleep_for(std::chrono::milliseconds(25));
		}
		const auto& last = result.samples.back();
		if (last.ok) {
			if (!Where(*state, last.rip).empty() && Readable(last.rip - 48, 96)) {
				const auto* bytes = reinterpret_cast<const uint8_t*>(last.rip - 48);
				result.code.assign(bytes, bytes + 96);
			}
			const uint64_t from = last.rsp;
			const uint64_t to   = thread.stack_high > from ? std::min<uint64_t>(thread.stack_high, from + 64 * 1024)
			                                               : from + 64 * 1024;
			for (uint64_t at = from; at + 8 <= to && result.stack.size() < 24; at += 8) {
				if ((at == from || (at & 0xfff) == 0) && !Readable(at, 8)) break;
				uint64_t value = 0;
				std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
				if (!Where(*state, value).empty()) {
					result.stack.push_back(at - from);
					result.stack.push_back(value);
				}
			}
		}
		results.push_back(std::move(result));
	}

	std::scoped_lock lock(state->mutex);
	Print(*state, "\n===== Guest threads, round %d =====\n", round);
	for (const auto& result: results) {
		const auto& last = result.samples.back();
		if (!last.ok) {
			Print(*state, "\n[%d] %s: could not sample\n", result.id, result.thread.name.c_str());
			continue;
		}
		Print(*state, "\n[%d] %s (os thread %" PRIu64 ")\n", result.id, result.thread.name.c_str(),
		      result.thread.host_thread_id);
		for (const auto& sample: result.samples) {
			if (!sample.ok) continue;
			const auto guest = Where(*state, sample.rip);
			Print(*state, "  rip 0x%016" PRIx64 "  %s\n", sample.rip,
			      guest.empty() ? HostWhere(sample.rip).c_str() : guest.c_str());
		}
		if (!result.code.empty()) {
			std::string text = "  code (rip-48..rip+48):";
			for (size_t i = 0; i < result.code.size(); i++) {
				char hex[8];
				std::snprintf(hex, sizeof(hex), "%s%02x", i == 48 ? " [" : " ", result.code[i]);
				text += hex;
			}
			text += "\n";
			Write(*state, text.c_str());
		}
		for (size_t i = 0; i + 1 < result.stack.size(); i += 2) {
			Print(*state, "  stack +0x%04" PRIx64 ": %s\n", result.stack[i], Where(*state, result.stack[i + 1]).c_str());
		}
	}
	Print(*state, "\n===== end of round %d =====\n", round);
}
#endif

void Watchdog() {
	const auto seconds = Seconds();
	for (int round = 1; round <= 3; round++) {
		std::this_thread::sleep_for(std::chrono::seconds(seconds));
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		Dump(round);
#else
		auto&            state = Get();
		std::scoped_lock lock(state.mutex);
		Print(state, "\nround %d: guest thread sampling is Windows only\n", round);
#endif
	}
}

} // namespace

bool Enabled() {
	return Seconds() > 0;
}

void ThreadStarted(int unique_id, const std::string& name, uint64_t host_thread_id, uint64_t stack_low,
                   uint64_t stack_high) {
	if (!Enabled()) return;
	auto& state = Get();
	{
		std::scoped_lock lock(state.mutex);
		state.threads[unique_id] = {name, host_thread_id, stack_low, stack_high};
	}
	std::call_once(state.started, [] {
		std::thread(Watchdog).detach();
		auto&            state = Get();
		std::scoped_lock lock(state.mutex);
		Print(state, "KYTY_HANG_PROBE=%d: guest threads sampled after %d s, 3 rounds\n", Seconds(), Seconds());
		for (const auto& module: state.modules) {
			Print(state, "module %s: 0x%016" PRIx64 " size 0x%" PRIx64 "\n", module.name.c_str(), module.base,
			      module.size);
		}
	});
}

void ThreadEnded(int unique_id) {
	if (!Enabled()) return;
	auto&            state = Get();
	std::scoped_lock lock(state.mutex);
	state.threads.erase(unique_id);
}

void ModuleLoaded(const std::string& name, uint64_t base, uint64_t size) {
	if (!Enabled()) return;
	auto&            state = Get();
	std::scoped_lock lock(state.mutex);
	state.modules.push_back({name, base, size});
}

void GpuWait(int queue, const char* kind, const void* address, uint64_t value, uint64_t reference, uint64_t mask,
             uint32_t function, bool passed) {
	if (!Enabled()) return;
	auto&            state = Get();
	std::scoped_lock lock(state.mutex);
	if (passed) {
		auto found = state.waits.find(queue);
		if (found != state.waits.end()) {
			if (found->second.reported) {
				Print(state, "GPU queue %d: the %s wait on 0x%016" PRIx64 " passed after %.1f s\n", queue,
				      found->second.kind, reinterpret_cast<uint64_t>(address),
				      std::chrono::duration<double>(Clock::now() - found->second.since).count());
			}
			state.waits.erase(found);
		}
		return;
	}
	auto& wait = state.waits[queue];
	if (wait.address != address || wait.reference != reference || wait.mask != mask || wait.function != function) {
		wait = {address, reference, mask, function, kind, Clock::now(), false};
		return;
	}
	if (!wait.reported && Clock::now() - wait.since > std::chrono::seconds(3)) {
		wait.reported = true;
		Print(state,
		      "GPU queue %d: %s waiting > 3 s on 0x%016" PRIx64 ": value 0x%" PRIx64 ", wants (value & 0x%" PRIx64
		      ") func %u 0x%" PRIx64 "\n",
		      queue, kind, reinterpret_cast<uint64_t>(address), value, mask, function, reference);
	}
}

} // namespace Kernel::HangProbe
