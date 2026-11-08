#include "graphics/host_gpu/renderer/hostStall.h"

#include "common/profiler.h"
#include "common/threads.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/host_gpu/renderer/commandRecorder.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/debugSample.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/presentation/videoOut.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
#include <tlhelp32.h> // IWYU pragma: keep
// IWYU pragma: no_include <winbase.h>
// IWYU pragma: no_include <winnt.h>
// IWYU pragma: no_include <processenv.h>
// IWYU pragma: no_include <handleapi.h>
// IWYU pragma: no_include <processthreadsapi.h>
#endif

namespace Libs::Graphics {

struct HostStallWatchdog::Impl {
	Impl(GuestGpu& gpu, RenderContext& renderer)
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	    : m_gpu(gpu), m_renderer(renderer)
#endif
	{
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		const char* enabled = std::getenv("KYTY_DIAG_HOST_STALL");
		if (enabled == nullptr || enabled[0] != '1') return;
		if (const char* seconds = std::getenv("KYTY_DIAG_HOST_STALL_SEC"); seconds != nullptr) {
			const auto value = std::strtoul(seconds, nullptr, 10);
			if (value != 0) m_stall_after = std::chrono::seconds(value);
		}
		m_thread = std::jthread([this](std::stop_token stop) { Run(stop); });
#else
		(void)gpu;
		(void)renderer;
#endif
	}

	void Stop() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		if (m_thread.joinable()) {
			m_thread.request_stop();
			m_thread.join();
		}
#endif
	}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	using Clock = std::chrono::steady_clock;

	struct Progress {
		uint64_t cp_loop = 0;
		uint64_t gpu_loop = 0;
		uint64_t gpu_progress_age_ms = 0;
		uint64_t present = 0;
		uint64_t recorder_published = 0;
		uint64_t recorder_recorded = 0;
		uint64_t sched_tick = 0;
		uint64_t sem_known = 0;
	};

	GuestGpu&                 m_gpu;
	RenderContext&            m_renderer;
	std::jthread              m_thread;
	std::chrono::seconds      m_stall_after {5};

	[[nodiscard]] Progress Sample() const {
		auto& scheduler = m_renderer.GetCommandScheduler();
		auto& master = scheduler.GetMasterSemaphore();
		const auto recorder = CommandRecorder::GetDebugState();
		return Progress {
		    .cp_loop = m_gpu.DebugCpLoopIterations(),
		    .gpu_loop = m_gpu.DebugGpuLoopIterations(),
		    .gpu_progress_age_ms = m_gpu.DebugProgressAgeMs(),
		    .present = DebugPresentCount().load(std::memory_order_relaxed),
		    .recorder_published = recorder.published,
		    .recorder_recorded = recorder.recorded,
		    .sched_tick = scheduler.CurrentTick(),
		    .sem_known = master.KnownGpuTick(),
		};
	}

	[[nodiscard]] static bool SameActivity(const Progress& a, const Progress& b) {
		return a.cp_loop == b.cp_loop && a.gpu_loop == b.gpu_loop && a.present == b.present &&
		       a.recorder_published == b.recorder_published && a.recorder_recorded == b.recorder_recorded &&
		       a.sched_tick == b.sched_tick && a.sem_known == b.sem_known;
	}

	[[nodiscard]] static std::string ThreadName(DWORD tid) {
		using GetThreadDescriptionFunc = HRESULT(WINAPI*)(HANDLE, PWSTR*);
		static const auto get_description = [] {
			const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
			return kernel32 == nullptr ? static_cast<GetThreadDescriptionFunc>(nullptr)
			                           : reinterpret_cast<GetThreadDescriptionFunc>(reinterpret_cast<void*>(
			                                 GetProcAddress(kernel32, "GetThreadDescription")));
		}();
		if (get_description == nullptr) return {};
		HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
		if (thread == nullptr) return {};
		PWSTR wide = nullptr;
		const HRESULT result = get_description(thread, &wide);
		CloseHandle(thread);
		if (FAILED(result) || wide == nullptr) return {};
		std::string name;
		for (const wchar_t* c = wide; *c != 0; ++c) {
			const wchar_t ch = *c;
			name.push_back(ch > 32 && ch < 127 ? static_cast<char>(ch) : ch == 32 ? '_' : '?');
		}
		LocalFree(wide);
		return name;
	}

	[[nodiscard]] static std::string ModuleFrame(DWORD64 pc) {
		HMODULE module = nullptr;
		const auto address = reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(pc));
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       address, &module) == 0 || module == nullptr) {
			char unknown[64];
			std::snprintf(unknown, sizeof(unknown), "unknown+0x%llx", static_cast<unsigned long long>(pc));
			return unknown;
		}
		wchar_t path[MAX_PATH] {};
		const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
		std::wstring_view view(path, length);
		const auto separator = view.find_last_of(L"\\/");
		if (separator != std::wstring_view::npos) view.remove_prefix(separator + 1);
		std::string name;
		if (module == GetModuleHandleW(nullptr)) {
			name = "exe";
		} else {
			for (const wchar_t ch: view) name.push_back(ch > 0 && ch < 128 ? static_cast<char>(ch) : '?');
			if (name.empty()) name = "unknown";
		}
		const auto offset = pc - reinterpret_cast<DWORD64>(module);
		char suffix[32];
		std::snprintf(suffix, sizeof(suffix), "+0x%llx", static_cast<unsigned long long>(offset));
		return name + suffix;
	}

	struct OpenedThread {
		HANDLE handle = nullptr;
		bool suspended = false;
		~OpenedThread() {
			if (suspended) ResumeThread(handle);
			if (handle != nullptr) CloseHandle(handle);
		}
	};

	static void DumpThread(const THREADENTRY32& entry) {
		const std::string name = ThreadName(entry.th32ThreadID);
		std::vector<std::string> frames;
		unsigned long error = 0;
		if (entry.th32ThreadID != GetCurrentThreadId()) {
			OpenedThread thread;
			thread.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
			                           entry.th32ThreadID);
			if (thread.handle == nullptr) {
				error = GetLastError();
			} else if (SuspendThread(thread.handle) == static_cast<DWORD>(-1)) {
				error = GetLastError();
			} else {
				thread.suspended = true;
				CONTEXT context {};
				context.ContextFlags = CONTEXT_FULL;
				if (GetThreadContext(thread.handle, &context) == 0) {
					error = GetLastError();
				} else {
					for (uint32_t index = 0; index < 48 && context.Rip != 0; ++index) {
						frames.push_back(ModuleFrame(context.Rip));
						DWORD64 image_base = 0;
						auto* function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
						if (function == nullptr) break;
						const DWORD64 old_ip = context.Rip;
						const DWORD64 old_sp = context.Rsp;
						PVOID handler_data = nullptr;
						DWORD64 establisher_frame = 0;
						(void)RtlVirtualUnwind(0, image_base, old_ip, function, &context, &handler_data,
						                       &establisher_frame, nullptr);
						if (context.Rip == old_ip && context.Rsp == old_sp) break;
					}
				}
			}
		}
		std::fprintf(stderr, "HOST_THREAD tid=%lu name=%s", static_cast<unsigned long>(entry.th32ThreadID),
		             name.empty() ? "-" : name.c_str());
		if (error != 0) std::fprintf(stderr, " error=%lu", error);
		std::fprintf(stderr, " frames=");
		for (const auto& frame: frames) std::fprintf(stderr, " %s", frame.c_str());
		std::fprintf(stderr, "\n");
	}

	void Dump(const Progress& progress) {
		const auto scheduler = m_renderer.GetCommandScheduler().GetDebugState();
		const auto present_depth = m_renderer.GetVideoOut().State().GetFlipQueue().DebugDepth();
		std::fprintf(stderr,
		             "HOST_STALL begin present=%llu gpu_progress_age_ms=%llu sched_tick=%llu sem_known=%llu\n",
		             static_cast<unsigned long long>(progress.present),
		             static_cast<unsigned long long>(progress.gpu_progress_age_ms),
		             static_cast<unsigned long long>(progress.sched_tick),
		             static_cast<unsigned long long>(progress.sem_known));
		std::fprintf(stderr,
		             "HOST_STALL scheduler pending=%lld oldest_pending_tick=%lld priority_active=%d "
		             "priority_operations=%lld deferred_callbacks=%u present_queue_depth=%lld "
		             "recorder_published=%llu recorder_recorded=%llu\n",
		             static_cast<long long>(scheduler.pending), static_cast<long long>(scheduler.oldest_pending_tick),
		             scheduler.priority_active, static_cast<long long>(scheduler.priority_operations),
		             scheduler.deferred_callbacks, static_cast<long long>(present_depth),
		             static_cast<unsigned long long>(progress.recorder_published),
		             static_cast<unsigned long long>(progress.recorder_recorded));
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE) {
			std::fprintf(stderr, "HOST_STALL threads_error=%lu\n", static_cast<unsigned long>(GetLastError()));
		} else {
			THREADENTRY32 entry {};
			entry.dwSize = sizeof(entry);
			if (Thread32First(snapshot, &entry) != 0) {
				do {
					if (entry.th32OwnerProcessID == GetCurrentProcessId()) DumpThread(entry);
				} while (Thread32Next(snapshot, &entry) != 0);
			} else {
				std::fprintf(stderr, "HOST_STALL threads_error=%lu\n", static_cast<unsigned long>(GetLastError()));
			}
			CloseHandle(snapshot);
		}
		std::fprintf(stderr, "HOST_STALL end\n");
		std::fflush(stderr);
	}

	void Run(std::stop_token stop) {
		KYTY_PROFILER_THREAD("Thread_HostStall");
		Progress previous = Sample();
		auto last_change = Clock::now();
		auto last_dump = Clock::time_point {};
		uint32_t dumps = 0;
		while (!stop.stop_requested()) {
			for (uint32_t i = 0; i != 10 && !stop.stop_requested(); ++i) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (stop.stop_requested() || m_gpu.DebugStopping()) return;
			const auto now = Clock::now();
			const Progress current = Sample();
			if (!SameActivity(previous, current)) {
				previous = current;
				last_change = now;
				continue;
			}
			if (dumps >= 3 || now - last_change < m_stall_after ||
			    (dumps != 0 && now - last_dump < std::chrono::seconds(30))) {
				continue;
			}
			Dump(current);
			++dumps;
			last_dump = Clock::now();
			last_change = last_dump;
			previous = Sample();
		}
	}
#endif
};

HostStallWatchdog::HostStallWatchdog(GuestGpu& gpu, RenderContext& renderer)
    : m_impl(std::make_unique<Impl>(gpu, renderer)) {}

HostStallWatchdog::~HostStallWatchdog() {
	Stop();
}

void HostStallWatchdog::Stop() {
	m_impl->Stop();
}

} // namespace Libs::Graphics
