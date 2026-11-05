#include "graphics/host_gpu/renderer/gpuWaitStats.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

void Check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "GPU wait stats test failed: " << message << '\n';
		std::exit(1);
	}
}

} // namespace

int main() {
#if defined(_WIN32)
		Check(_putenv("KYTY_WAIT_STATS=1") == 0, "could not enable wait stats");
#else
		Check(setenv("KYTY_WAIT_STATS", "1", 1) == 0, "could not enable wait stats");
#endif
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
			const auto expected = reason + " wait_ms=" + std::to_string(i) +
			                      ".0 waits=1 submits=0;";
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
