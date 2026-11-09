#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_PIPELINECOMPILEDIAG_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_PIPELINECOMPILEDIAG_H_

#include "common/common.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <cstdint>
#include <span>

namespace Libs::Graphics::ShaderRecompiler::IR {
struct CompiledShaderInfo;
} // namespace Libs::Graphics::ShaderRecompiler::IR

namespace Libs::Graphics::PipelineCompileDiag {

struct Record;

enum class Kind {
	Graphics,
	Compute,
};

struct Stage {
	const char*             name = "unknown";
	std::span<const uint32_t> spirv {};
	uint64_t                guest_address = 0;
};

[[nodiscard]] bool Enabled();
[[nodiscard]] uint64_t SpirvHash(std::span<const uint32_t> spirv);
void PrepareShader(ShaderRecompiler::IR::CompiledShaderInfo& info, std::span<const uint32_t> spirv,
                   uint64_t guest_address);
[[nodiscard]] Stage ShaderStage(const char* name, const ShaderRecompiler::IR::CompiledShaderInfo* info);
[[nodiscard]] bool DisableOptimization(Kind kind, std::span<const Stage> stages);
// 1 when the driver reported an application pipeline cache hit, 0 when it reported a miss, -1 when it gave no feedback.
[[nodiscard]] inline int CacheHit(const vk::PipelineCreationFeedback& feedback) {
	if (!(feedback.flags & vk::PipelineCreationFeedbackFlagBits::eValid)) return -1;
	return (feedback.flags & vk::PipelineCreationFeedbackFlagBits::eApplicationPipelineCacheHit) ? 1 : 0;
}

class Compile {
public:
	Compile(Kind kind, uint64_t key_hash, std::span<const Stage> stages);
	~Compile() = default;
	KYTY_CLASS_NO_COPY(Compile);

	// cache_hit: VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT of the pipeline feedback
	// (1 hit, 0 miss, -1 not reported), printed as cache_hit= on the PIPELINE_COMPILE line.
	void End(vk::Result result, int cache_hit = -1);

private:
	Record*  m_record = nullptr;
	uint32_t m_slot   = 0;
};

void DumpInflight(const char* prefix);

} // namespace Libs::Graphics::PipelineCompileDiag

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_PIPELINECOMPILEDIAG_H_
