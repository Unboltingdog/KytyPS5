#include "graphics/host_gpu/renderer/cache/faultReadback.h"
#include "graphics/host_gpu/renderer/gpuWaitStats.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

void Check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "GPU wait stats test failed: " << message << '\n';
		std::exit(1);
	}
}

void SetFaultWaitOffGpu(const char* value) {
#if defined(_WIN32)
	const std::string setting = value != nullptr ? std::string("KYTY_FAULT_WAIT_OFF_GPU=") + value
	                                             : "KYTY_FAULT_WAIT_OFF_GPU=";
	Check(_putenv(setting.c_str()) == 0, "could not change off-GPU fault waits");
#else
	if (value != nullptr) {
		Check(setenv("KYTY_FAULT_WAIT_OFF_GPU", value, 1) == 0,
		      "could not change off-GPU fault waits");
	} else {
		Check(unsetenv("KYTY_FAULT_WAIT_OFF_GPU") == 0, "could not clear off-GPU fault waits");
	}
#endif
}

void TestFaultReadbackBegin() {
	using namespace Libs::Graphics;
	struct Readback {
		uint64_t                          begin;
		uint64_t                          end;
		uint64_t                          tick;
		std::shared_ptr<std::atomic_bool> published;
	};
	struct Scheduler {
		uint32_t flushes        = 0;
		uint32_t waits          = 0;
		uint32_t priority_waits = 0;
		uint64_t tick           = 0;
		void     Flush() { flushes++; }
		void     Wait(uint64_t value) {
			waits++;
			tick = value;
		}
		void WaitPriorityOperations(uint64_t value) {
			priority_waits++;
			tick = value;
		}
	};

	auto published = std::make_shared<std::atomic_bool>(false);
	const std::array readbacks {Readback {0x1000, 0x2000, 7, published}};

	SetFaultWaitOffGpu("1");
	Check(FaultReadback::GetWaitMode(true, false, false) == FaultReadback::WaitMode::OffGpu,
	      "the switch did not enable off-GPU fault waits");
	Scheduler off_gpu_scheduler;
	const auto off_gpu = FaultReadback::Begin(readbacks, 0x1100, 1,
	                                          FaultReadback::GetWaitMode(true, false, false),
	                                          off_gpu_scheduler);
	Check(off_gpu.found_readback && off_gpu.waits.size() == 1 && off_gpu.waits[0] == published &&
	          !published->load() && off_gpu_scheduler.flushes == 1 && off_gpu_scheduler.waits == 0 &&
	          off_gpu_scheduler.priority_waits == 0,
	      "an in-flight fault readback blocked instead of returning its published atomic");

	SetFaultWaitOffGpu("0");
	Scheduler synchronous_scheduler;
	const auto synchronous = FaultReadback::Begin(
	    readbacks, 0x1100, 1, FaultReadback::GetWaitMode(true, false, false), synchronous_scheduler);
	Check(synchronous.waits.empty() && synchronous.wait_tick == 7 &&
	          synchronous_scheduler.flushes == 0 && synchronous_scheduler.waits == 1 &&
	          synchronous_scheduler.priority_waits == 1 && synchronous_scheduler.tick == 7,
	      "switch-off behavior did not preserve the legacy scheduler wait");

	SetFaultWaitOffGpu("1");
	Check(FaultReadback::GetWaitMode(false, false, false) == FaultReadback::WaitMode::Synchronous &&
	          FaultReadback::GetWaitMode(true, true, false) == FaultReadback::WaitMode::Synchronous &&
	          FaultReadback::GetWaitMode(true, false, true) == FaultReadback::WaitMode::Synchronous,
	      "non-fault, GPU-thread, or deferred callers left the synchronous path");
	SetFaultWaitOffGpu(nullptr);
}

} // namespace

int main() {
#if defined(_WIN32)
	Check(_putenv("KYTY_WAIT_STATS=1") == 0, "could not enable wait stats");
#else
	Check(setenv("KYTY_WAIT_STATS", "1", 1) == 0, "could not enable wait stats");
#endif
	TestFaultReadbackBegin();
	std::remove("kyty-diag.txt");
	Libs::Graphics::GpuWaitStats::RecordEvent("warmup");
	for (uint64_t i = 1; i <= 6u; ++i) {
		const auto reason = "reason-" + std::to_string(i);
		Libs::Graphics::GpuWaitStats::Scope scope(reason.c_str());
		Libs::Graphics::GpuWaitStats::RecordWait(i * 1'000'000u);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(2100));
	Libs::Graphics::GpuWaitStats::RecordEvent("flush");

	std::ifstream input("kyty-diag.txt");
	Check(input.good(), "diagnostic log was not created");
	std::string line;
	std::string wait_top;
	while (std::getline(input, line)) {
		if (line.starts_with("wait-top")) wait_top = line;
	}
	input.close();
	Check(!wait_top.empty(), "wait-top line was not written");
	for (uint64_t i = 2; i <= 6u; ++i) {
		const auto reason = "reason-" + std::to_string(i);
		const auto position = wait_top.find(reason);
		Check(position != std::string::npos, "a top-five wait reason is missing");
		const auto expected = reason + " wait_ms=" + std::to_string(i) + ".0 waits=1 submits=0;";
		Check(wait_top.find(expected) != std::string::npos,
		      "wait reason has the wrong milliseconds or count");
	}
	Check(wait_top.find("reason-1 ") == std::string::npos,
	      "wait-top included the sixth-ranked reason");
	Check(wait_top.find("flush") == std::string::npos,
	      "wait-top included an event instead of a wait reason");
#if !defined(_WIN32)
	std::remove("kyty-diag.txt");
#endif
	std::cout << "GPU wait stats tests passed\n";
	return 0;
}
