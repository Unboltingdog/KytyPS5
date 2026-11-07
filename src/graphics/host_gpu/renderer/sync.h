#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_SYNC_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_SYNC_H_

#include "graphics/host_gpu/renderer/gpuEventDiagnostics.h"
#include "kernel/eventQueue.h"

#include <cstdint>

namespace Libs::Graphics {

class CommandBuffer;
class Buffer;
class RenderContext;

namespace Sync {

[[nodiscard]] bool     ScaleReferenceClock(uint64_t host_ticks, uint64_t host_frequency,
                                           uint64_t& value);
[[nodiscard]] uint64_t ReadReferenceClock();

[[nodiscard]] uint64_t PrepareVideoOutFlip(CommandBuffer& buffer, int handle, int index,
                                           int flip_mode, int64_t flip_arg);
int  AddEqEvent(RenderContext& renderer, LibKernel::EventQueue::KernelEqueue eq, int id,
                void* udata, uint64_t caller = 0);
int  DeleteEqEvent(RenderContext& renderer, LibKernel::EventQueue::KernelEqueue eq, int id,
                   uint64_t caller = 0);
void ReadGds(const Buffer& gds, uint32_t* dst, uint32_t dw_offset, uint32_t dw_size);

} // namespace Sync
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_SYNC_H_
