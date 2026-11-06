#include "graphics/shader/recompiler/backend/spirv/IfConversionAnalysis.h"

namespace Libs::Graphics::ShaderRecompiler::Spirv {
namespace {

bool UnsafeOpcode(IR::ValueOpcode opcode) {
	using Op = IR::ValueOpcode;
	switch (opcode) {
		case Op::Ballot:
		case Op::LiveMask:
		case Op::ConditionRef:
		case Op::ReadFirstLane:
		case Op::ReadLane:
		case Op::WriteLane:
		case Op::Permlane16U32:
		case Op::PermuteU32:
		case Op::BpermuteU32:
		case Op::SwizzleU32:
		case Op::DppMoveU32:
		case Op::DppUpdateU32:
		case Op::SubgroupUMinU32:
		case Op::SubgroupUMaxU32:
		case Op::SubgroupOrU32:
		case Op::SetExec:
		case Op::SetExecLo:
		case Op::SetExecHi: return true;
		default: return false;
	}
}

} // namespace

bool IfConversionUnsafeOpcode(IR::ValueOpcode opcode) {
	return UnsafeOpcode(opcode);
}

IfConversionShape AnalyzeIfConversion(const IR::Program& program) {
	IfConversionShape result;
	for (const auto* block: program.blocks) {
		IR::Value condition;
		size_t    first   = 0;
		size_t    last    = 0;
		uint64_t  selects = 0;
		const auto flush  = [&] {
			if (selects < 2u) {
				return;
			}
			result.regions++;
			result.instructions += last - first + 1u;
			bool unsafe = false;
			size_t index = 0;
			for (const auto& inst: *block) {
				if (index >= first && index <= last) {
					unsafe |= inst.MayHaveSideEffects() || UnsafeOpcode(inst.GetOpcode());
				}
				index++;
			}
			result.unsafe_regions += unsafe;
		};
		size_t index = 0;
		for (const auto& inst: *block) {
			if (inst.GetOpcode() == IR::ValueOpcode::SelectU32) {
				const auto next = inst.Arg(0).Resolve();
				if (condition.IsEmpty() || next != condition) {
					flush();
					condition = next;
					first     = index;
					selects   = 0;
				}
				last = index;
				selects++;
			}
			index++;
		}
		flush();
	}
	return result;
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv
