#include "graphics/shader/recompiler/backend/spirv/SpirvShape.h"

#include <spirv/unified1/spirv.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "SpirvShapeTests: " << message << '\n';
		std::exit(1);
	}
}

void Append(std::vector<uint32_t>& module, spv::Op opcode,
            std::initializer_list<uint32_t> operands = {}) {
	module.push_back((static_cast<uint32_t>(operands.size() + 1u) << spv::WordCountShift) | opcode);
	module.insert(module.end(), operands.begin(), operands.end());
}

} // namespace

int main() {
	using Libs::Graphics::ShaderRecompiler::Spirv::AnalyzeModuleShape;
	Check(!AnalyzeModuleShape({}), "empty input was accepted");
	Check(!AnalyzeModuleShape(std::vector<uint32_t> {spv::MagicNumber, 0, 0, 1, 0, 0}),
	      "a zero-length instruction was accepted");

	std::vector<uint32_t> module {spv::MagicNumber, 0x00010300u, 0, 32, 0};
	Append(module, spv::OpFunction, {1, 2, 0, 3});
	Append(module, spv::OpLabel, {4});
	Append(module, spv::OpBitcast, {5, 6, 7});
	Append(module, spv::OpBitcast, {5, 8, 9});
	Append(module, spv::OpSelect, {5, 10, 11, 12, 13});
	Append(module, spv::OpPhi, {5, 14, 15, 4});
	Append(module, spv::OpExtInst, {5, 16, 17, 0});
	Append(module, spv::OpFunctionCall, {5, 18, 19});
	Append(module, spv::OpLoopMerge, {20, 4, 0});
	Append(module, spv::OpFunctionEnd);
	Append(module, spv::OpFunction, {1, 21, 0, 3});
	Append(module, spv::OpFunctionEnd);
	const auto shape = AnalyzeModuleShape(module);
	Check(shape.has_value(), "valid module was rejected");
	Check(shape->largest_function_instructions == 10u, "largest function count is wrong");
	Check(shape->bitcasts == 2u && shape->selects == 1u && shape->phis == 1u,
	      "data-flow opcode counts are wrong");
	Check(shape->ext_insts == 1u && shape->function_calls == 1u && shape->loop_merges == 1u,
	      "instruction/merge counts are wrong");
	std::cout << "SpirvShapeTests: all cases passed\n";
	return 0;
}
