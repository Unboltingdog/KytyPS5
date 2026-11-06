#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVOPTIONS_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVOPTIONS_H_

#include <cstdint>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

bool     TypedVgprsEnabled();
uint32_t IfConversionLimit();
bool     OutlineHelpersEnabled();

} // namespace Libs::Graphics::ShaderRecompiler::Spirv

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SPIRVOPTIONS_H_ */
