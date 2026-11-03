#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <cstdint>
#include <cstdlib>
#include <string>

namespace Libs::Graphics::ShaderRecompiler::IR {

// Resolves native descriptor sources, plans their scalar reads, and assigns dense resource bindings.
// KYTY_GPU_SCALAR_LOADS=1: keep scalar loads that only feed shader math and branches as GPU
// loads instead of dispatch constants the CPU reads. Changes compiled shaders.
inline bool GpuScalarLoadsEnabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_GPU_SCALAR_LOADS");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}
// KYTY_GPU_BUFFER_TABLES=1: leave indirect buffer-table selection to the shader.
inline bool GpuBufferTablesEnabled() {
	const char* value = std::getenv("KYTY_GPU_BUFFER_TABLES");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}
inline bool GpuBufferTableEnabled(const DescriptorSource::IndirectDescriptor& indirect) {
	return GpuBufferTablesEnabled() && indirect.sources.empty();
}
// KYTY_STABLE_SRT_LAYOUT=1: use fixed indirect-map headers and a separate variable-length tail.
inline bool StableSrtLayoutEnabled() {
	const char* value = std::getenv("KYTY_STABLE_SRT_LAYOUT");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}
void TrackResources(Program& program, const Decoder::Program& decoded, const CFG::Graph& native_cfg,
                    ShaderStageInputInfo input);

// A compute shader with an access TrackResources cannot place yet (Unboltingdog fork): it goes on
// without it and leaves the first one here, for the calling thread; the pipeline cache takes it
// and skips the shader's dispatches.
struct UnsupportedCompute {
	uint32_t    pc = UINT32_MAX;
	std::string reason;
};
[[nodiscard]] UnsupportedCompute TakeUnsupportedCompute();
// While one lives, on its thread, TrackResources may leave such a compute access out; elsewhere
// it stays fatal.
class AllowUnsupportedCompute {
public:
	AllowUnsupportedCompute();
	~AllowUnsupportedCompute();
	AllowUnsupportedCompute(const AllowUnsupportedCompute&)            = delete;
	AllowUnsupportedCompute& operator=(const AllowUnsupportedCompute&) = delete;

private:
	bool m_previous;
};

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_ */
