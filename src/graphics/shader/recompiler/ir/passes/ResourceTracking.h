#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <cstdint>
#include <string>

namespace Libs::Graphics::ShaderRecompiler::IR {

// Resolves native descriptor sources, plans their scalar reads, and assigns dense resource bindings.
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
