#include "graphics/shader/recompiler/backend/spirv/SpirvOptions.h"

#include <cstdlib>
#include <cstring>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

bool TypedVgprsEnabled() {
	const char* value = std::getenv("KYTY_TYPED_VGPRS");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

uint32_t IfConversionLimit() {
	const char* value = std::getenv("KYTY_IFCONV_LIMIT");
	if (value == nullptr || value[0] == '\0' || value[0] == '0') {
		return 0;
	}
	if (std::strcmp(value, "1") == 0) {
		return 32;
	}
	char*      end = nullptr;
	const auto parsed = std::strtoul(value, &end, 10);
	return end != value && end != nullptr && *end == '\0' && parsed <= UINT32_MAX
	           ? static_cast<uint32_t>(parsed)
	           : 0;
}

bool OutlineHelpersEnabled() {
	const char* value = std::getenv("KYTY_OUTLINE_HELPERS");
	return value != nullptr && value[0] != '\0' && value[0] != '0';
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv
