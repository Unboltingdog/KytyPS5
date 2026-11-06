#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_HOSTIMPORTPROBE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_HOSTIMPORTPROBE_H_

#include <cstdint>
#include <optional>

namespace Libs::Graphics {

struct GraphicContext;

namespace HostImportProbe {

inline constexpr uint64_t ProbeWindowSize = 64ull * 1024ull * 1024ull;

struct Window {
	uint64_t address = 0;
	uint64_t size    = 0;
};

[[nodiscard]] constexpr std::optional<Window> SelectWindow(uint64_t mapping_address,
                                                           uint64_t mapping_size,
                                                           uint64_t alignment) noexcept {
	if (mapping_size < ProbeWindowSize || alignment == 0 || (alignment & (alignment - 1u)) != 0 ||
	    mapping_address > UINT64_MAX - mapping_size) {
		return std::nullopt;
	}
	const auto adjustment = (alignment - (mapping_address & (alignment - 1u))) & (alignment - 1u);
	if (adjustment > mapping_size - ProbeWindowSize) {
		return std::nullopt;
	}
	return Window {.address = mapping_address + adjustment, .size = ProbeWindowSize};
}

[[nodiscard]] bool Enabled() noexcept;
void               LogDeviceCapabilities(const GraphicContext& graphics);
void               ProbeFirstDirectMapping(GraphicContext& graphics, uint64_t mapping_address,
                                           uint64_t mapping_size);

} // namespace HostImportProbe
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_HOSTIMPORTPROBE_H_
