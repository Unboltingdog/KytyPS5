#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_IFCONVERSIONANALYSIS_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_IFCONVERSIONANALYSIS_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

namespace Libs::Graphics::ShaderRecompiler::Spirv {

struct IfConversionShape {
	uint64_t regions          = 0;
	uint64_t instructions     = 0;
	uint64_t unsafe_regions   = 0;
};

IfConversionShape AnalyzeIfConversion(const IR::Program& program);

} // namespace Libs::Graphics::ShaderRecompiler::Spirv

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_IFCONVERSIONANALYSIS_H_ */
