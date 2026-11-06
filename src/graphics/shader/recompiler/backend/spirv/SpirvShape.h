#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVSHAPE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVSHAPE_H_

#include <cstdint>
#include <optional>
#include <span>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

struct ModuleShape {
	uint64_t largest_function_instructions = 0;
	uint64_t bitcasts                      = 0;
	uint64_t selects                       = 0;
	uint64_t phis                          = 0;
	uint64_t ext_insts                     = 0;
	uint64_t function_calls                = 0;
	uint64_t loop_merges                   = 0;
};

std::optional<ModuleShape> AnalyzeModuleShape(std::span<const uint32_t> spirv);

} // namespace Libs::Graphics::ShaderRecompiler::Spirv

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVSHAPE_H_ */
