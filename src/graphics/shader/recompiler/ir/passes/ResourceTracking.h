#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace Libs::Graphics::ShaderRecompiler::IR {

class ResourceTrackingError : public std::runtime_error {
public:
	ResourceTrackingError(std::string message, uint32_t pc, std::string reason):
	    std::runtime_error(std::move(message)), pc(pc), reason(std::move(reason)) {}

	uint32_t    pc;
	std::string reason;
};

enum class ScalarReadPlanner { Default, Flat, GpuChain, GpuChainDiagnostic, GpuChainNoHybrids };

// Resolves native descriptor sources, plans their scalar reads, and assigns dense resource bindings.
void TrackResources(Program& program, const Decoder::Program& decoded, const CFG::Graph& native_cfg,
                    ScalarReadPlanner planner = ScalarReadPlanner::Default);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCETRACKING_H_ */
