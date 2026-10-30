#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUCRASHREPORT_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUCRASHREPORT_H_

#include "graphics/host_gpu/vulkanCommon.h"
#include "graphics/shader/shader.h"

#include <cstdint>

// What the GPU was running when Vulkan lost the device (Unboltingdog fork). The pipeline cache notes
// every shader it hands out with the command buffer tick it goes into; when a wait or a counter
// read fails, the report (console, log, and gpu-crash.txt next to the emulator) gives the result,
// the device fault (VK_EXT_device_fault, where the driver has it: faulting addresses, vendor
// codes) and the shaders of the submissions the GPU had not finished.
namespace Libs::Graphics {

struct GraphicContext;
class MasterSemaphore;

namespace GpuCrashReport {

// The semaphore whose ticks the notes carry.
// The render scheduler's semaphore: shader notes carry its ticks.
void SetMaster(const MasterSemaphore* master);
void ClearMaster(const MasterSemaphore* master);
// A shader goes into the current command buffer.
void NoteShader(uint64_t hash, ShaderType stage);
// The device is lost (or a wait or counter read failed with `result`): writes the report.
// `completed` is the last tick the GPU was known to finish.
void Report(GraphicContext& graphics, vk::Result result, uint64_t completed);

} // namespace GpuCrashReport
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUCRASHREPORT_H_
