#include "graphics/host_gpu/hostImportProbe.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "Host import probe test failed: %s\n", message);
		std::exit(1);
	}
}

} // namespace

int main() {
	using Libs::Graphics::HostImportProbe::ProbeWindowSize;
	using Libs::Graphics::HostImportProbe::SelectWindow;
	constexpr uint64_t MiB = 1024ull * 1024ull;

	const auto aligned = SelectWindow(0x20000000, 128 * MiB, 64 * 1024);
	Check(aligned.has_value() && aligned->address == 0x20000000 && aligned->size == ProbeWindowSize,
	      "an aligned mapping did not select its first 64 MiB");
	const auto rounded = SelectWindow(0x20000003, 128 * MiB, 64 * 1024);
	Check(rounded.has_value() && rounded->address == 0x20010000,
	      "window selection did not round the host pointer up");
	Check(!SelectWindow(0x20000003, ProbeWindowSize, 64 * 1024).has_value(),
	      "rounding beyond the mapping was accepted");
	Check(!SelectWindow(0x20000000, ProbeWindowSize - MiB, 64 * 1024).has_value(),
	      "a mapping smaller than 64 MiB was accepted");
	Check(!SelectWindow(0x20000000, 128 * MiB, 0).has_value() &&
	          !SelectWindow(0x20000000, 128 * MiB, 3).has_value(),
	      "a zero or non-power-of-two alignment was accepted");
	Check(!SelectWindow(UINT64_MAX - MiB, 2 * MiB, 64 * 1024).has_value(),
	      "an overflowing mapping was accepted");

	std::puts("Host import probe tests passed");
	return 0;
}
