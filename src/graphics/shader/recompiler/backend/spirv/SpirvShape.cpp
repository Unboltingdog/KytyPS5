#include "graphics/shader/recompiler/backend/spirv/SpirvShape.h"

#include <spirv/unified1/spirv.hpp>

#include <algorithm>

namespace Libs::Graphics::ShaderRecompiler::Spirv {

std::optional<ModuleShape> AnalyzeModuleShape(std::span<const uint32_t> spirv) {
	if (spirv.size() < 5u || spirv[0] != spv::MagicNumber) {
		return std::nullopt;
	}
	ModuleShape result;
	uint64_t    function_instructions = 0;
	for (size_t offset = 5u; offset < spirv.size();) {
		const auto header = spirv[offset];
		const auto words  = static_cast<size_t>(header >> spv::WordCountShift);
		if (words == 0u || words > spirv.size() - offset) {
			return std::nullopt;
		}
		const auto opcode = static_cast<spv::Op>(header & spv::OpCodeMask);
		switch (opcode) {
			case spv::OpFunction: function_instructions = 1; break;
			case spv::OpFunctionEnd:
				if (function_instructions != 0u) {
					result.largest_function_instructions =
					    std::max(result.largest_function_instructions, ++function_instructions);
					function_instructions = 0;
				}
				break;
			case spv::OpBitcast: result.bitcasts++; break;
			case spv::OpSelect: result.selects++; break;
			case spv::OpPhi: result.phis++; break;
			case spv::OpExtInst: result.ext_insts++; break;
			case spv::OpFunctionCall: result.function_calls++; break;
			case spv::OpLoopMerge: result.loop_merges++; break;
			default: break;
		}
		if (function_instructions != 0u && opcode != spv::OpFunction) {
			function_instructions++;
		}
		offset += words;
	}
	if (function_instructions != 0u) {
		return std::nullopt;
	}
	return result;
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv
