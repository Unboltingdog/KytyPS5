#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_

#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"
#include <string>
#include <string_view>

namespace Libs::Graphics::ShaderRecompiler::IR {

inline constexpr uint32_t RuntimeIndirectSearchIterations = UINT32_MAX;

// Canonical module-affecting resource state. Runtime addresses and descriptor payloads remain in
// ResourceSnapshot and therefore do not create shader permutations.
struct ResourceSpecialization {
	struct Buffer {
		uint32_t               packed_stride                   = 0;
		Prospero::BufferFormat descriptor_format               = Prospero::BufferFormat::kInvalid;
		uint32_t               descriptor_swizzle              = DstSel(4, 5, 6, 7);
		bool                   zero_stride_oob                 = false;
		bool                   scalar_empty                    = false;
		uint32_t               indirect_root                   = BufferResource::NoIndirectBuffer;
		uint32_t               indirect_mapping_offset         = 0;
		uint32_t               indirect_search_iterations      = 0;
		bool                   operator==(const Buffer&) const = default;
	};

	struct Image {
		Prospero::TextureNumericClass numeric_class = Prospero::TextureNumericClass::Unsupported;
		Decoder::ImageDimension       dimension     = Decoder::ImageDimension::Unknown;
		uint32_t                      mip_count     = 1;
		Prospero::BufferFormat        conversion_format          = Prospero::BufferFormat::kInvalid;
		uint32_t                      shader_swizzle             = ShaderImageIdentitySwizzle;
		uint32_t                      indirect_root              = ImageResource::NoIndirectImage;
		uint32_t                      indirect_mapping_offset    = 0;
		uint32_t                      indirect_search_iterations = 0;
		bool                          cube                       = false;
		bool                          fmask                      = false;
		bool                          heap                       = false;
		bool                          operator==(const Image&) const = default;
	};

	struct Sampler {
		uint32_t indirect_root = SamplerResource::NoIndirectSampler;
		uint32_t indirect_mapping_offset = 0;
		uint32_t indirect_search_iterations = 0;
		bool     heap = false;
		bool operator==(const Sampler&) const = default;
	};

	std::vector<Buffer> buffers;
	std::vector<Image>  images;
	std::vector<Sampler> samplers;

	bool operator==(const ResourceSpecialization&) const = default;
};

std::string ResourceSpecializationDiff(const ResourceSpecialization& before,
                                       const ResourceSpecialization& after,
                                       std::string_view prefix = {});
void NormalizeStableGraphicsSpec(ResourceSpecialization& specialization);

// Extracts the descriptor/SRT value graph before resource specialization. The returned plan owns
// its values and is independent of the translated shader CFG.
ResourcePlan ExtractResourcePlan(const Program& program);

// Shader cache record of a plan; loading it into a default-constructed plan rebuilds the walker
// and scratch state like extraction. A failed load leaves a plan that must not be used.
std::vector<uint8_t> SaveResourcePlan(const ResourcePlan& plan);
bool                 LoadResourcePlan(std::span<const uint8_t> bytes, ResourcePlan& plan);

// Refreshes cached resources and specialization in place. A failed refresh must not be used.
// The kind of specialization read in progress on this thread, for KYTY_WAIT_STATS.
const char* SpecializationReadSite();
// Nanoseconds spent running SRT walkers on this thread since the last call.
uint64_t TakeWalkNanoseconds();
struct HeapFallback {
	const char* kind;
	uint32_t    resource;
	const char* reason;
	uint64_t    table_size;
};
// Tables materialized inline despite heap eligibility on this thread since the last call.
std::vector<HeapFallback> TakeHeapFallbacks();
struct LargeTableSkip {
	const char* kind;
	uint32_t    resource;
	uint64_t    candidates;
	uint32_t    limit;
	uint64_t    table_size;
};
// The large inline table rejected on this thread since the last call, if any.
bool TakeLargeTableSkip(LargeTableSkip& skip);
// The first failure a KYTY_WALKER_NO_STALL materialization reported on this thread, then clears it.
std::string TakeNoStallFailure();
// KYTY_WALKER_NO_STALL: whether the read in progress may be refused when the GPU has written its
// memory, because the caller can do without it; the reader then notes the refusal.
bool SpecializationReadMayDefer();
void NoteSpecializationReadDeferred();
bool MaterializeResources(const ResourcePlan& program, const SrtRuntime& runtime,
                          ResourceSnapshot& snapshot, ResourceSpecialization& specialization);

// Applies an already-derived specialization to native IR before layout and emission.
void ApplyResourceSpecialization(Program& program, const ResourceSpecialization& specialization);

// Kind of an image heap entry as root sees it, or 0 (and a zeroed value) when root reads it as
// null. Entries of one kind specialize identically.
uint32_t ImageHeapKind(const ImageResource& root, DescriptorValue& value);
// The sampled resource that root resolves entries of kind as; kind 0 gives the identity of
// root's view of a heap.
ImageResource ImageHeapResource(const ImageResource& root, uint32_t kind);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_ */
