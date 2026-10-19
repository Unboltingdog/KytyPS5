// The quality presets' render heights (KYTY_UPSCALE_QUALITY, src/loader/demonsSoulsRenderSize.h QualityHeight).
#include "loader/demonsSoulsRenderSize.h"

#include <cstdio>
#include <cstdlib>

namespace {

using Loader::DemonsSoulsRenderSize::QualityHeight;

void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "UpscaleQualityTests: failed: %s\n", message);
		std::abort();
	}
}

void TestPresets() {
	// The output's 2160 divided by the ratio, a multiple of 8.
	Check(QualityHeight("native") == 2160, "native is not 2160");
	Check(QualityHeight("ultraquality") == 1664, "ultra quality (1.3x) is not 1664");
	Check(QualityHeight("quality") == 1440, "quality (1.5x) is not 1440");
	Check(QualityHeight("balanced") == 1272, "balanced (1.7x) is not 1272");
	Check(QualityHeight("performance") == 1080, "performance (2x) is not 1080");
	Check(QualityHeight("ultraperformance") == 720, "ultra performance (3x) is not 720");
	for (const auto& preset: Loader::DemonsSoulsRenderSize::QualityPresets) {
		Check(QualityHeight(preset.name) % 8 == 0, "a preset's height is not a multiple of 8");
	}
}

void TestRatios() {
	Check(QualityHeight("1.3") == QualityHeight("ultraquality"), "1.3 is not ultra quality");
	Check(QualityHeight("2") == 1080, "2 is not 1080");
	Check(QualityHeight("1") == 2160, "1 is not 2160");
	Check(QualityHeight("3") == 720, "3 is not 720");
}

void TestRejected() {
	Check(QualityHeight("") == 0, "an empty value was taken");
	Check(QualityHeight("Quality") == 0, "presets are lower case");
	Check(QualityHeight("0.5") == 0, "a ratio below 1 was taken");
	Check(QualityHeight("4") == 0, "a ratio above 3 was taken");
	Check(QualityHeight("1.2.3") == 0, "a malformed number was taken");
	Check(QualityHeight("fast") == 0, "an unknown name was taken");
}

} // namespace

int main() {
	TestPresets();
	TestRatios();
	TestRejected();
	std::printf("UpscaleQualityTests: passed\n");
	return 0;
}
