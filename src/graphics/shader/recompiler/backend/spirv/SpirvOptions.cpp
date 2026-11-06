#include "graphics/shader/recompiler/backend/spirv/SpirvOptions.h"

#include <cstdlib>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

bool TypedVgprsEnabled() {
	const char* value = std::getenv("KYTY_TYPED_VGPRS");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv
