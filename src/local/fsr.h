#ifndef EMULATOR_SRC_LOCAL_FSR_H_
#define EMULATOR_SRC_LOCAL_FSR_H_
// AMD FSR 3.1 through the FidelityFX API (3rdparty/fidelityfx, AMD's signed amd_fidelityfx_vk.dll
// loaded at run time), a second backend beside NVIDIA Streamline (frame-gen.h) for the same Demon's
// Souls inputs:
// - KYTY_UPSCALER=fsr: FSR 3.1 upscaling in place of Sony's PS5 Pro upscaler on the game's PS5 Pro
// path
//   (renderSuperResolution.cpp), as KYTY_UPSCALER=dlss puts DLSS there. Any GPU with Vulkan 1.3.
// - KYTY_FRAMEGEN_BACKEND=fsr with KYTY_FRAMEGEN=1: FSR 3.1 frame generation (one generated frame
// per rendered one,
//   2x), from the depth, motion vectors and camera frame-gen.cpp gathers for DLSS-G. The swapchain
//   becomes FidelityFX's frame interpolation swapchain (its own present and pacing threads, on
//   queues of its own: PlanQueues).
// - KYTY_FSR_SHARPNESS=<0..1> (default 0.25): the upscaler's RCAS sharpening (0: none); hotkeys
// change it live.
// - KYTY_FFX_DIR: the folder holding amd_fidelityfx_vk.dll (default: next to the emulator).
// - KYTY_FSR_VERBOSE=1: FidelityFX's messages and the first dispatches' inputs on stdout;
// KYTY_FSR_DEBUG=1 also turns on
//   its input checks (FFX_UPSCALE_ENABLE_DEBUG_CHECKING) and FG's debug tear and pacing lines.
// FSR 4 has no Vulkan runtime (AMD ships it for DirectX 12 only), so none is offered here.
#include "frame-gen.h"
#include "graphics/host_gpu/graphicContext.h"

#include <cstdint>
#include <vector>

namespace Libs::Graphics::FrameGen::Fsr {

// Process start (FrameGen::Initialize): reads the switches above and loads the runtime when one
// asks for it.
void Initialize();
// Asked for, and the runtime loaded (before the device).
[[nodiscard]] bool UpscaleRequested();
[[nodiscard]] bool FrameGenRequested();

// Before the device: the extra queues FSR frame generation's swapchain wants (present, image
// acquire) as (family, count) to add to the device's queue create infos; empty
// without frame generation. `graphics_family` is the emulator's queue family; `graphics_used` the
// queues it takes of it (1, or 2 with its present queue).
struct QueueSlot {
	uint32_t family = UINT32_MAX;
	uint32_t index  = 0;
};
std::vector<std::pair<uint32_t, uint32_t>>
PlanQueues(vk::PhysicalDevice physical_device, vk::SurfaceKHR surface, uint32_t graphics_family,
           uint32_t graphics_used, uint32_t readback_family);
// Before the device: features FidelityFX's shaders take for granted where the GPU has them (16-bit
// floats).
void AddDeviceFeatures(vk::PhysicalDevice                  physical_device,
                       vk::PhysicalDeviceVulkan12Features& features12,
                       vk::PhysicalDeviceFeatures&         features);
// ... and 16-bit storage (its 16-bit shaders): true when `storage` is to be chained to the device.
[[nodiscard]] bool Add16BitStorage(vk::PhysicalDevice                      physical_device,
                                   vk::PhysicalDevice16BitStorageFeatures& storage);
// After the device and its queues: checks the runtime against it, takes the planned queues.
void OnDevice(GraphicContext& graphics);

// FSR upscaling is ready on the device.
[[nodiscard]] bool UpscalerReady();
// Records FSR upscaling into `command` (outside a render pass; color, depth and motion in
// shader-read layouts, the output general): false when it could not.
[[nodiscard]] bool Upscale(vk::CommandBuffer command, const UpscaleInputs& inputs);

// FSR frame generation is ready: the swapchain goes through FidelityFX (the calls below in place of
// Vulkan's).
[[nodiscard]] bool FrameGenReady();
vk::Result CreateSwapchain(GraphicContext& graphics, const vk::SwapchainCreateInfoKHR& create_info,
                           vk::SwapchainKHR* swapchain);
void       DestroySwapchain(GraphicContext& graphics, vk::SwapchainKHR swapchain);
vk::Result GetSwapchainImages(GraphicContext& graphics, vk::SwapchainKHR swapchain, uint32_t* count,
                              vk::Image* images);
vk::Result AcquireNextImage(GraphicContext& graphics, vk::SwapchainKHR swapchain, uint64_t timeout,
                            vk::Semaphore semaphore, uint32_t* index);
vk::Result QueuePresent(GraphicContext& graphics, const vk::PresentInfoKHR& present);

// Present thread, every present, in three steps (FidelityFX wants the preparation and the
// configuration before the present):
// - BeginPresent, before the renderer's lock: whether generation is on for this frame, its frameID,
//   the context made (or made again). `inputs` null: a frame without them (a menu, a movie, an idle
//   refresh); generation pauses after a run of those. `overlay`: the emulator's own menu is up.
// - RecordPresent, under it, with the frame's commands: its preparation (depth, motion, camera).
// - ConfigurePresent, after the commands are submitted, before the present: its configuration
//   (turning generation on or off waits for the queues, whose locks it takes then).
void BeginPresent(const FrameInputs* inputs, vk::Extent2D backbuffer, vk::Format format, bool hdr,
                  bool overlay);
void RecordPresent(vk::CommandBuffer command, const FrameInputs* inputs);
void ConfigurePresent(const FrameInputs* inputs, vk::Rect2D region);
// Frames shown per rendered frame (2 while generating), for the frame rate panel.
[[nodiscard]] uint32_t PresentedFrames();
// A frame with inputs was left unshown: the next one resets frame generation's history.
void NoteDroppedFrame();

// Live settings (hotkeys): the upscaler's sharpness (0..1).
[[nodiscard]] float Sharpness();
void                SetSharpness(float value);

} // namespace Libs::Graphics::FrameGen::Fsr

#endif // EMULATOR_SRC_LOCAL_FSR_H_
