#include "graphics/shader/recompiler/backend/spirv/SpirvShape.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: spirv_shape FILE.spv\n";
		return 2;
	}
	std::ifstream input(argv[1], std::ios::binary);
	if (!input) {
		std::cerr << "spirv_shape: cannot open " << argv[1] << '\n';
		return 1;
	}
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
	                                 std::istreambuf_iterator<char>());
	if (bytes.size() % sizeof(uint32_t) != 0u) {
		std::cerr << "spirv_shape: file size is not a whole number of SPIR-V words\n";
		return 1;
	}
	std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
	std::memcpy(words.data(), bytes.data(), bytes.size());
	const auto shape = Libs::Graphics::ShaderRecompiler::Spirv::AnalyzeModuleShape(words);
	if (!shape) {
		std::cerr << "spirv_shape: malformed SPIR-V module\n";
		return 1;
	}
	std::cout << "spirv-shape file=" << argv[1]
	          << " largest_function_instructions=" << shape->largest_function_instructions
	          << " bitcasts=" << shape->bitcasts << " selects=" << shape->selects
	          << " phis=" << shape->phis << " ext_insts=" << shape->ext_insts
	          << " function_calls=" << shape->function_calls
	          << " loop_merges=" << shape->loop_merges
	          << " ifconv_regions=unknown ifconv_instructions=unknown ifconv_unsafe=unknown\n";
	return 0;
}
