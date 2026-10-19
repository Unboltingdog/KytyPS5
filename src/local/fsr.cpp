#include "fsr.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/threads.h"
#include "frame-gen-hudless.h"

// The FidelityFX API (3rdparty/fidelityfx/include): C headers; the runtime (amd_fidelityfx_vk.dll)
// is loaded below.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_types.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef min
#undef max
#else
#include <dlfcn.h>
#endif

// (A macro: the compiler checks each message's format against its arguments.)
#define FSR_PRINT(...)                                                                             \
	do {                                                                                           \
		std::printf(__VA_ARGS__);                                                                  \
		std::fflush(stdout);                                                                       \
	} while (false)

namespace Libs::Graphics::FrameGen::Fsr {

namespace {

// The runtime's five entry points.
struct Api {
	PfnFfxCreateContext  create    = nullptr;
	PfnFfxDestroyContext destroy   = nullptr;
	PfnFfxConfigure      configure = nullptr;
	PfnFfxQuery          query     = nullptr;
	PfnFfxDispatch       dispatch  = nullptr;
};
Api  g_api;
bool g_want_upscale = false; // KYTY_UPSCALER=fsr, the runtime loaded
bool g_want_fg      = false; // KYTY_FRAMEGEN_BACKEND=fsr with KYTY_FRAMEGEN, the runtime loaded
bool g_verbose      = false; // KYTY_FSR_VERBOSE / KYTY_FSR_DEBUG
bool g_debug        = false; // KYTY_FSR_DEBUG

GraphicContext*         g_graphics     = nullptr;
PFN_vkGetDeviceProcAddr g_get_proc     = nullptr; // the driver's, not a layer's copy
PFN_vkQueueSubmit       g_queue_submit = nullptr;
std::atomic<bool>       g_upscale_ready {false};
std::atomic<bool>       g_fg_ready {false};
std::atomic<uint32_t>   g_sharpness_milli {250}; // KYTY_FSR_SHARPNESS, hotkeys

// The queues frame generation's swapchain takes (PlanQueues): present and image acquire (no async
// compute queue: its work stays on the present thread).
QueueSlot g_present_slot, g_acquire_slot;
vk::Queue g_present_queue = nullptr, g_acquire_queue = nullptr;
// separateDepthStencilLayouts enabled (AddDeviceFeatures): FidelityFX's depth barriers name the
// depth aspect alone, valid on a depth-stencil image only with it.
bool g_separate_depth_stencil = false;

// The emulator's queues held while FidelityFX's swapchain may wait for its game queue
// (vkQueueWaitIdle) or the whole device (vkDeviceWaitIdle): when frame generation is turned on or
// off, and when its contexts are made or destroyed (FrameInterpolationSwapchainVK waitForPresents).
// (Common::Mutex is recursive: SubmitGame takes the queue's lock again inside.)
class QueueLocks {
public:
	QueueLocks() {
		g_graphics->queue_mutex.Lock();
		g_graphics->present_queue_mutex.Lock();
		ReadbackQueueMutex().Lock();
	}
	~QueueLocks() {
		ReadbackQueueMutex().Unlock();
		g_graphics->present_queue_mutex.Unlock();
		g_graphics->queue_mutex.Unlock();
	}
	QueueLocks(const QueueLocks&)            = delete;
	QueueLocks& operator=(const QueueLocks&) = delete;
};

bool Ok(ffxReturnCode_t code) {
	return code == FFX_API_RETURN_OK;
}

void Message(uint32_t type, const wchar_t* message) {
	if (!g_verbose && type != FFX_API_MESSAGE_TYPE_ERROR) return;
	std::printf("FSR %s: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message);
	std::fflush(stdout);
}

ffxCreateBackendVKDesc BackendDesc() {
	ffxCreateBackendVKDesc backend {};
	backend.header.type      = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
	backend.header.pNext     = nullptr;
	backend.vkDevice         = static_cast<VkDevice>(g_graphics->device);
	backend.vkPhysicalDevice = static_cast<VkPhysicalDevice>(g_graphics->physical_device);
	backend.vkDeviceProcAddr = g_get_proc;
	return backend;
}

bool HasStencil(vk::Format format) {
	return format == vk::Format::eD16UnormS8Uint || format == vk::Format::eD24UnormS8Uint ||
	       format == vk::Format::eD32SfloatS8Uint;
}

bool IsDepth(vk::Format format) {
	switch (format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32Sfloat:
		case vk::Format::eD32SfloatS8Uint:
		case vk::Format::eX8D24UnormPack32: return true;
		default: return false;
	}
}

// A 2D image as FidelityFX takes it: the whole image (FidelityFX's views and barriers cover all of
// its levels and layers: the callers hand over images of one of each), in `state`
// (FFX_API_RESOURCE_STATE_*, the layout that goes with it), `format` the one to view it as.
FfxApiResource Resource(vk::Image image, vk::Format format, vk::Extent2D extent, uint32_t levels,
                        uint32_t layers, uint32_t state, bool storage) {
	FfxApiResource resource {};
	resource.resource             = static_cast<void*>(static_cast<VkImage>(image));
	resource.state                = state;
	resource.description.type     = FFX_API_RESOURCE_TYPE_TEXTURE2D;
	resource.description.format   = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(format));
	resource.description.width    = extent.width;
	resource.description.height   = extent.height;
	resource.description.depth    = std::max(layers, 1u);
	resource.description.mipCount = std::max(levels, 1u);
	resource.description.flags    = FFX_API_RESOURCE_FLAGS_NONE;
	resource.description.usage    = FFX_API_RESOURCE_USAGE_READ_ONLY;
	if (IsDepth(format)) resource.description.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;
	if (HasStencil(format)) resource.description.usage |= FFX_API_RESOURCE_USAGE_STENCILTARGET;
	if (storage) resource.description.usage |= FFX_API_RESOURCE_USAGE_UAV;
	return resource;
}

FfxApiResource Resource(const UpscaleImage& image, uint32_t state, bool storage) {
	// (A view of another format than the image's: the image is mutable, FidelityFX views it as the
	// view's.)
	return Resource(image.image,
	                image.view_format != vk::Format::eUndefined ? image.view_format : image.format,
	                image.image_extent.width != 0 ? image.image_extent : image.extent, image.levels,
	                image.layers, state, storage);
}

FfxApiResource Resource(const VulkanImage& image, uint32_t state) {
	return Resource(image.image, image.format, {image.extent.width, image.extent.height},
	                image.mip_levels, image.layers, state, false);
}

float MillisecondsSince(std::chrono::steady_clock::time_point& last) {
	const auto now = std::chrono::steady_clock::now();
	float      ms  = last == std::chrono::steady_clock::time_point {}
	                     ? 16.7f
	                     : std::chrono::duration<float, std::milli>(now - last).count();
	last           = now;
	return std::clamp(ms, 1.0f, 100.0f);
}

// KYTY_FSR_JITTER=<x sign><y sign> (a test: "+-", "-+", "--"): the jitter's signs as handed over;
// default "++".
std::array<float, 2> SignPair(const char* name) {
	const char* value = std::getenv(name);
	if (value == nullptr || std::strlen(value) < 2) return {1.0f, 1.0f};
	return {value[0] == '-' ? -1.0f : 1.0f, value[1] == '-' ? -1.0f : 1.0f};
}

// ---- Upscaling ----

struct UpscaleContext {
	ffxContext   context = nullptr;
	vk::Extent2D max_render {}, output {};
	uint32_t     flags = 0;
};
std::mutex     g_upscale_mutex;
UpscaleContext g_upscale;
// Replaced contexts, destroyed some dispatches later (their last work may still be on the GPU).
std::vector<std::pair<ffxContext, uint32_t>> g_upscale_retired;
std::chrono::steady_clock::time_point        g_upscale_last {};
uint64_t                                     g_upscale_count = 0;

bool EnsureUpscaleContext(vk::Extent2D render, vk::Extent2D output, uint32_t flags) {
	auto& current = g_upscale;
	if (current.context != nullptr && current.output == output && current.flags == flags &&
	    render.width <= current.max_render.width && render.height <= current.max_render.height)
		return true;
	// (Destroyed 120 dispatches - about two seconds - later: long after the GPU's last use of it,
	// without waiting on queues from the thread that records. It is made again only when the
	// render or output size changes.)
	if (current.context != nullptr) g_upscale_retired.emplace_back(current.context, 120u);
	current                             = {};
	auto                        backend = BackendDesc();
	ffxCreateContextDescUpscale desc {};
	desc.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	desc.header.pNext = &backend.header;
	desc.flags        = flags;
	// (Its render-size resources at the render size: a bigger one later makes it again.)
	desc.maxRenderSize  = {render.width, render.height};
	desc.maxUpscaleSize = {output.width, output.height};
	desc.fpMessage      = Message;
	if (!Ok(g_api.create(&current.context, &desc.header, nullptr)) || current.context == nullptr) {
		current = {};
		FSR_PRINT("FSR upscaling: the context could not be made (%ux%u -> %ux%u); the image is "
		          "stretched instead\n",
		          render.width, render.height, output.width, output.height);
		return false;
	}
	current.max_render = {desc.maxRenderSize.width, desc.maxRenderSize.height};
	current.output     = output;
	current.flags      = flags;
	ffxQueryGetProviderVersion version {};
	version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
	(void)g_api.query(&current.context, &version.header);
	FSR_PRINT("FSR upscaling: %s, %ux%u -> %ux%u (%.2fx)\n",
	          version.versionName != nullptr ? version.versionName : "FSR 3.1", render.width,
	          render.height, output.width, output.height,
	          static_cast<double>(output.height) / std::max(1u, render.height));
	return true;
}

void RetireOld() {
	for (auto it = g_upscale_retired.begin(); it != g_upscale_retired.end();) {
		if (--it->second == 0) {
			(void)g_api.destroy(&it->first, nullptr);
			it = g_upscale_retired.erase(it);
		} else {
			++it;
		}
	}
}

// ---- Frame generation ----

// FidelityFX's swapchain, its replacement functions, and the frame generation context made on it.
ffxContext                                  g_swapchain_context = nullptr;
VkSwapchainKHR                              g_swapchain         = VK_NULL_HANDLE;
ffxQueryDescSwapchainReplacementFunctionsVK g_functions {};
ffxContext                                  g_fg_context = nullptr;
struct FgShape {
	vk::Extent2D display {}, max_render {};
	vk::Format   format = vk::Format::eUndefined;
	bool         hdr = false, hudless = false;
	bool         operator==(const FgShape&) const = default;
};
FgShape  g_fg_shape;
uint64_t g_frame_id = 0;
bool     g_mode_on  = false;
uint32_t g_run_with = 0, g_run_without = 0;
// The configure of this present (ConfigurePresent), for its preparation (RecordPresent); the
// frameID last prepared, which the generation checks its frame against.
struct PendingPresent {
	uint64_t frame_id = 0;
	float    frame_ms = 16.7f;
	bool     prepare  = false; // the frame has inputs
	bool     mode_on  = false; // generation on for it
	bool     overlay  = false;
	bool     valid    = false; // BeginPresent decided this present
};
PendingPresent        g_pending;
std::atomic<uint64_t> g_last_prepared {0};
// The enabled state last configured (-1: none since the context was made): a change makes
// FidelityFX wait for its queues.
int                                   g_configured_enabled = -1;
std::atomic<bool>                     g_reset_next {true};
std::atomic<bool>                     g_hdr_now {false};
std::chrono::steady_clock::time_point g_fg_last {};
std::atomic<uint32_t>                 g_presented {1};
uint64_t                              g_count_frames = 0, g_count_presents = 0;
constexpr uint32_t TurnOffAfter = 30, TurnOnAfter = 10; // (as DLSS-G's: frame-gen.cpp)

VkResult SubmitGame(uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
	// (The emulator's own queue: its submissions hold its lock.)
	Common::LockGuard lock(g_graphics->queue_mutex);
	return g_queue_submit(static_cast<VkQueue>(g_graphics->queue), count, submits, fence);
}

VkQueueInfoFFXAPI QueueInfo(vk::Queue queue, uint32_t family, PFN_vkQueueSubmitFFXAPI submit) {
	VkQueueInfoFFXAPI info {};
	info.queue       = static_cast<VkQueue>(queue);
	info.familyIndex = family;
	info.submitFunc  = submit;
	return info;
}

ffxReturnCode_t GenerateFrame(ffxDispatchDescFrameGeneration* params, void* user) {
	// The swapchain's call (in its present, on the present thread): the frame between the last two.
	params->backbufferTransferFunction = g_hdr_now.load(std::memory_order_relaxed)
	                                         ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ
	                                         : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
	params->minMaxLuminance[0]         = 0.0001f;
	params->minMaxLuminance[1]         = 1000.0f;
	if (g_reset_next.exchange(false, std::memory_order_relaxed)) params->reset = true;
	// A frame without its own preparation (a menu, an idle refresh while generation stays on): what
	// the context holds is an older frame's, so no interpolation from history.
	if (params->frameID != g_last_prepared.load(std::memory_order_relaxed)) params->reset = true;
	return g_api.dispatch(static_cast<ffxContext*>(user), &params->header);
}

void DestroyFrameGenContext() {
	if (g_fg_context == nullptr) return;
	const QueueLocks locks;
	// Off first, its presents done, then gone (FidelityFX's order).
	ffxConfigureDescFrameGeneration off {};
	off.header.type            = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
	off.swapChain              = reinterpret_cast<void*>(g_swapchain);
	off.frameGenerationEnabled = false;
	off.frameID                = ++g_frame_id;
	(void)g_api.configure(&g_fg_context, &off.header);
	if (g_swapchain_context != nullptr) {
		ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait {};
		wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
		(void)g_api.dispatch(&g_swapchain_context, &wait.header);
	}
	(void)g_api.destroy(&g_fg_context, nullptr);
	g_fg_context         = nullptr;
	g_fg_shape           = {};
	g_mode_on            = false;
	g_configured_enabled = -1;
	g_reset_next.store(true, std::memory_order_relaxed);
}

bool EnsureFrameGenContext(const FgShape& wanted) {
	if (g_fg_context != nullptr && g_fg_shape == wanted) return true;
	FgShape shape = wanted;
	if (g_fg_context != nullptr) {
		// (A bigger render size only grows it: a smaller one fits the context made for the bigger.)
		if (shape.display == g_fg_shape.display && shape.format == g_fg_shape.format &&
		    shape.hdr == g_fg_shape.hdr && shape.hudless == g_fg_shape.hudless &&
		    shape.max_render.width <= g_fg_shape.max_render.width &&
		    shape.max_render.height <= g_fg_shape.max_render.height)
			return true;
		shape.max_render = {std::max(shape.max_render.width, g_fg_shape.max_render.width),
		                    std::max(shape.max_render.height, g_fg_shape.max_render.height)};
		DestroyFrameGenContext();
	}
	auto                                       backend = BackendDesc();
	ffxCreateContextDescFrameGenerationHudless hudless {};
	hudless.header.type             = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
	hudless.header.pNext            = &backend.header;
	hudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(shape.format));
	ffxCreateContextDescFrameGeneration desc {};
	desc.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
	desc.header.pNext = shape.hudless ? &hudless.header : &backend.header;
	// (The game's depth is reversed-Z with an infinite far plane: frame-gen.cpp Decompose.)
	desc.flags =
	    FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
	if (shape.hdr) desc.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
	if (g_debug) desc.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
	desc.displaySize      = {shape.display.width, shape.display.height};
	desc.maxRenderSize    = {shape.max_render.width, shape.max_render.height};
	desc.backBufferFormat = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(shape.format));
	ffxReturnCode_t made  = FFX_API_RETURN_ERROR;
	{
		const QueueLocks locks; // (its failure path waits for the queues)
		made = g_api.create(&g_fg_context, &desc.header, nullptr);
	}
	g_configured_enabled = -1;
	if (!Ok(made) || g_fg_context == nullptr) {
		g_fg_context = nullptr;
		FSR_PRINT("FSR frame generation: the context could not be made (display %ux%u, render "
		          "%ux%u); frames shown as "
		          "rendered\n",
		          shape.display.width, shape.display.height, shape.max_render.width,
		          shape.max_render.height);
		return false;
	}
	g_fg_shape = shape;
	g_reset_next.store(true, std::memory_order_relaxed);
	FSR_PRINT("FSR frame generation: ready (display %ux%u, render up to %ux%u%s%s)\n",
	          shape.display.width, shape.display.height, shape.max_render.width,
	          shape.max_render.height, shape.hdr ? ", HDR" : "",
	          shape.hudless ? ", HUD-less image" : "");
	return true;
}

// The camera of the game's world->clip matrix (frame-gen.cpp's Decompose, without Streamline's
// types): a perspective whose w row is (0, 0, 1, 0) and whose z row is constant (reversed-Z,
// infinite far).
struct CameraFrame {
	std::array<float, 3> right {}, up {}, forward {}, position {};
	float                sy = 1.0f, z_near = 0.1f;
	bool                 valid = false;
};

CameraFrame Decompose(const std::array<float, 16>& m) {
	CameraFrame c;
	const auto  row = [&](uint32_t i) {
		return std::array<double, 4> {m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]};
	};
	const auto   r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
	const double flen = std::sqrt(r3[0] * r3[0] + r3[1] * r3[1] + r3[2] * r3[2]);
	if (flen < 1e-6) return c;
	const double f[3] {r3[0] / flen, r3[1] / flen, r3[2] / flen};
	const double tf    = r3[3] / flen;
	const auto   split = [&](const std::array<double, 4>& r, double& s, double axis[3], double& t) {
		const double off = (r[0] * f[0] + r[1] * f[1] + r[2] * f[2]) / flen;
		const double v[3] {r[0] / flen - off * f[0], r[1] / flen - off * f[1],
		                   r[2] / flen - off * f[2]};
		s = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (s < 1e-9) return false;
		for (int i = 0; i < 3; ++i)
			axis[i] = v[i] / s;
		t = (r[3] / flen - off * tf) / s;
		return true;
	};
	double sx = 0, sy = 0, x[3] {}, y[3] {}, tx = 0, ty = 0;
	if (!split(r0, sx, x, tx) || !split(r1, sy, y, ty)) return c;
	const double up[3] {f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2],
	                    f[0] * x[1] - f[1] * x[0]};
	if (up[0] * y[0] + up[1] * y[1] + up[2] * y[2] < 0) {
		sy = -sy;
		ty = -ty;
	}
	for (int i = 0; i < 3; ++i) {
		c.right[i]    = static_cast<float>(x[i]);
		c.up[i]       = static_cast<float>(up[i]);
		c.forward[i]  = static_cast<float>(f[i]);
		c.position[i] = static_cast<float>(-(tx * x[i] + ty * up[i] + tf * f[i]));
	}
	c.sy     = static_cast<float>(sy);
	c.z_near = static_cast<float>(r2[3] / flen);
	c.valid  = true;
	return c;
}

} // namespace

void Initialize() {
	const char* upscaler = std::getenv("KYTY_UPSCALER");
	const char* backend  = std::getenv("KYTY_FRAMEGEN_BACKEND");
	const char* frames   = std::getenv("KYTY_FRAMEGEN");
	g_want_upscale       = upscaler != nullptr && std::string_view(upscaler) == "fsr";
	g_want_fg = backend != nullptr && std::string_view(backend) == "fsr" && frames != nullptr &&
	            std::atoi(frames) > 0;
	g_debug   = std::getenv("KYTY_FSR_DEBUG") != nullptr;
	g_verbose = g_debug || std::getenv("KYTY_FSR_VERBOSE") != nullptr;
	if (const char* sharp = std::getenv("KYTY_FSR_SHARPNESS"); sharp != nullptr && *sharp != '\0')
		SetSharpness(static_cast<float>(std::atof(sharp)));
	if (!g_want_upscale && !g_want_fg) return;
	if (g_want_fg && frames != nullptr && std::atoi(frames) > 1)
		FSR_PRINT("FSR frame generation: one generated frame per rendered frame (FSR 3 makes 2x "
		          "only; KYTY_FRAMEGEN=%s taken "
		          "as 1)\n",
		          frames);

	// The runtime: KYTY_FFX_DIR, else next to the emulator.
	std::vector<std::filesystem::path> candidates;
#if defined(_WIN32)
	constexpr const char* Name = "amd_fidelityfx_vk.dll";
	wchar_t               exe[MAX_PATH] {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	const auto exe_dir = std::filesystem::path(exe).parent_path();
	if (const wchar_t* dir = _wgetenv(L"KYTY_FFX_DIR"); dir != nullptr && *dir != L'\0')
		candidates.emplace_back(std::filesystem::path(dir) / Name);
	candidates.push_back(exe_dir / Name);
	for (const auto& candidate: candidates) {
		std::error_code error;
		if (!std::filesystem::exists(candidate, error)) continue;
		if (HMODULE module = LoadLibraryW(candidate.c_str()); module != nullptr) {
			const auto load = [&](auto& function, const char* name) {
				function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
				    reinterpret_cast<void*>(GetProcAddress(module, name)));
				return function != nullptr;
			};
			if (load(g_api.create, "ffxCreateContext") &&
			    load(g_api.destroy, "ffxDestroyContext") && load(g_api.configure, "ffxConfigure") &&
			    load(g_api.query, "ffxQuery") && load(g_api.dispatch, "ffxDispatch")) {
				FSR_PRINT("FSR: AMD FidelityFX runtime loaded from %s\n",
				          candidate.string().c_str());
				break;
			}
			g_api = {};
		}
	}
#else
	// (AMD ships the runtime for Windows only; a libamd_fidelityfx_vk.so built from the SDK's
	// sources would do.)
	if (const char* dir = std::getenv("KYTY_FFX_DIR"); dir != nullptr && *dir != '\0')
		candidates.emplace_back(std::filesystem::path(dir) / "libamd_fidelityfx_vk.so");
	candidates.emplace_back("libamd_fidelityfx_vk.so");
	for (const auto& candidate: candidates) {
		if (void* module = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL); module != nullptr) {
			const auto load = [&](auto& function, const char* name) {
				function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
				    dlsym(module, name));
				return function != nullptr;
			};
			if (load(g_api.create, "ffxCreateContext") &&
			    load(g_api.destroy, "ffxDestroyContext") && load(g_api.configure, "ffxConfigure") &&
			    load(g_api.query, "ffxQuery") && load(g_api.dispatch, "ffxDispatch")) {
				FSR_PRINT("FSR: AMD FidelityFX runtime loaded from %s\n",
				          candidate.string().c_str());
				break;
			}
			g_api = {};
		}
	}
#endif
	if (g_api.create == nullptr) {
		FSR_PRINT("FSR: AMD's FidelityFX runtime (amd_fidelityfx_vk.dll) was not found next to the "
		          "emulator or in "
		          "KYTY_FFX_DIR; FSR is off\n");
		g_want_upscale = g_want_fg = false;
		return;
	}
	// (FidelityFX's messages come through each context's fpMessage: its global debug configure
	// takes no null context.)
}

bool UpscaleRequested() {
	return g_want_upscale;
}

bool FrameGenRequested() {
	return g_want_fg;
}

std::vector<std::pair<uint32_t, uint32_t>>
PlanQueues(vk::PhysicalDevice physical_device, vk::SurfaceKHR surface, uint32_t graphics_family,
           uint32_t graphics_used, uint32_t readback_family) {
	g_present_slot = g_acquire_slot = {};
	if (!g_want_fg) return {};
	uint32_t family_count = 0;
	physical_device.getQueueFamilyProperties(&family_count, nullptr);
	std::vector<vk::QueueFamilyProperties> families(family_count);
	physical_device.getQueueFamilyProperties(&family_count, families.data());
	// The queues the emulator itself takes: index 0 (and 1 with its present queue) of its family,
	// the readback's 0.
	std::vector<uint32_t> next(family_count, 0);
	if (graphics_family < family_count) next[graphics_family] = graphics_used;
	if (readback_family < family_count) next[readback_family] = std::max(next[readback_family], 1u);
	const auto presents = [&](uint32_t family) {
		vk::Bool32 supported = VK_FALSE;
		return surface != nullptr &&
		       physical_device.getSurfaceSupportKHR(family, surface, &supported) ==
		           vk::Result::eSuccess &&
		       supported == VK_TRUE;
	};
	const auto take = [&](uint32_t family) -> QueueSlot {
		if (family >= family_count || next[family] >= families[family].queueCount) return {};
		return {family, next[family]++};
	};
	const auto has = [&](uint32_t family, vk::QueueFlags flags) {
		return (families[family].queueFlags & flags) == flags;
	};
	const auto compute_only = [&](uint32_t family) {
		return has(family, vk::QueueFlagBits::eCompute) &&
		       !has(family, vk::QueueFlagBits::eGraphics);
	};
	const auto transfer_only = [&](uint32_t family) {
		return has(family, vk::QueueFlagBits::eTransfer) &&
		       !has(family, vk::QueueFlagBits::eGraphics) &&
		       !has(family, vk::QueueFlagBits::eCompute);
	};
	// Present: a queue that can present and copy, not the emulator's: an async compute family's
	// (AMD has one graphics queue only), else a second graphics queue, else any other that
	// presents.
	for (uint32_t family = 0; family < family_count && g_present_slot.family == UINT32_MAX;
	     ++family)
		if (compute_only(family) && presents(family)) g_present_slot = take(family);
	if (g_present_slot.family == UINT32_MAX && presents(graphics_family))
		g_present_slot = take(graphics_family);
	for (uint32_t family = 0; family < family_count && g_present_slot.family == UINT32_MAX;
	     ++family)
		if (has(family, vk::QueueFlagBits::eTransfer) && presents(family))
			g_present_slot = take(family);
	if (g_present_slot.family == UINT32_MAX) {
		FSR_PRINT("FSR frame generation: no queue of its own to present on beside the emulator's; "
		          "frame generation is off\n");
		g_want_fg = false;
		return {};
	}
	// Image acquire (any queue).
	for (uint32_t family = 0; family < family_count && g_acquire_slot.family == UINT32_MAX;
	     ++family)
		if (transfer_only(family)) g_acquire_slot = take(family);
	for (uint32_t family = 0; family < family_count && g_acquire_slot.family == UINT32_MAX;
	     ++family)
		if (family != graphics_family) g_acquire_slot = take(family);
	if (g_acquire_slot.family == UINT32_MAX) g_acquire_slot = take(graphics_family);
	if (g_acquire_slot.family == UINT32_MAX) {
		FSR_PRINT("FSR frame generation: no spare queue to acquire swapchain images on; frame "
		          "generation is off\n");
		g_want_fg = false;
		return {};
	}
	std::vector<std::pair<uint32_t, uint32_t>> wanted;
	for (uint32_t family = 0; family < family_count; ++family) {
		if (next[family] == 0) continue;
		wanted.emplace_back(family, next[family]);
	}
	FSR_PRINT("FSR frame generation: queues present %u.%u, acquire %u.%u (graphics family %u)\n",
	          g_present_slot.family, g_present_slot.index, g_acquire_slot.family,
	          g_acquire_slot.index, graphics_family);
	return wanted;
}

void AddDeviceFeatures(vk::PhysicalDevice                  physical_device,
                       vk::PhysicalDeviceVulkan12Features& features12,
                       vk::PhysicalDeviceFeatures&         features) {
	if (!g_want_upscale && !g_want_fg) return;
	// FidelityFX picks its 16-bit shader variants by what the GPU has, not by what the device
	// enabled.
	vk::PhysicalDeviceVulkan12Features supported12 {};
	vk::PhysicalDeviceFeatures2        supported {};
	supported.pNext = &supported12;
	physical_device.getFeatures2(&supported);
	if (supported12.shaderFloat16 == VK_TRUE) features12.shaderFloat16 = VK_TRUE;
	if (supported.features.shaderInt16 == VK_TRUE) features.shaderInt16 = VK_TRUE;
	if (supported12.separateDepthStencilLayouts == VK_TRUE) {
		features12.separateDepthStencilLayouts = VK_TRUE;
		g_separate_depth_stencil               = true;
	}
}

bool Add16BitStorage(vk::PhysicalDevice                      physical_device,
                     vk::PhysicalDevice16BitStorageFeatures& storage) {
	if (!g_want_upscale && !g_want_fg) return false;
	// (Its 16-bit shaders read and write 16-bit values in buffers: GL_EXT_shader_16bit_storage.)
	vk::PhysicalDevice16BitStorageFeatures supported16 {};
	vk::PhysicalDeviceFeatures2            supported {};
	supported.pNext = &supported16;
	physical_device.getFeatures2(&supported);
	storage.storageBuffer16BitAccess           = supported16.storageBuffer16BitAccess;
	storage.uniformAndStorageBuffer16BitAccess = supported16.uniformAndStorageBuffer16BitAccess;
	return storage.storageBuffer16BitAccess == VK_TRUE ||
	       storage.uniformAndStorageBuffer16BitAccess == VK_TRUE;
}

void OnDevice(GraphicContext& graphics) {
	if (!g_want_upscale && !g_want_fg) return;
	g_graphics = &graphics;
	// (The driver's own entry points: FidelityFX records and submits around any layer of the
	// emulator's.)
	g_get_proc     = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
	g_queue_submit = reinterpret_cast<PFN_vkQueueSubmit>(
	    g_get_proc(static_cast<VkDevice>(graphics.device), "vkQueueSubmit"));
	if (g_want_upscale) {
		g_upscale_ready = true;
		FSR_PRINT("FSR upscaling: ready (FSR 3.1, sharpness %.2f)\n",
		          static_cast<double>(Sharpness()));
	}
	if (g_want_fg && g_queue_submit != nullptr) {
		graphics.device.getQueue(g_present_slot.family, g_present_slot.index, &g_present_queue);
		graphics.device.getQueue(g_acquire_slot.family, g_acquire_slot.index, &g_acquire_queue);
		g_fg_ready = g_present_queue != nullptr && g_acquire_queue != nullptr;
		if (g_fg_ready) {
			InitializeHudless();
			FSR_PRINT("FSR frame generation: ready (2x)\n");
		} else {
			FSR_PRINT("FSR frame generation: its queues are missing; frame generation is off\n");
		}
	}
}

bool UpscalerReady() {
	return g_upscale_ready.load(std::memory_order_relaxed);
}

bool Upscale(vk::CommandBuffer command, const UpscaleInputs& inputs) {
	if (!UpscalerReady()) return false;
	std::lock_guard lock(g_upscale_mutex);
	RetireOld();
	// (FidelityFX views and transitions whole images; the renderer moved one level and layer of
	// each: images of one of each only. A depth with stencil needs separate depth-stencil layouts.)
	const auto single = [](const UpscaleImage& image) {
		return image.base_level == 0 && image.base_layer == 0 && image.levels == 1 &&
		       image.layers == 1;
	};
	if (!single(inputs.color) || !single(inputs.depth) || !single(inputs.motion) ||
	    !single(inputs.output) || (HasStencil(inputs.depth.format) && !g_separate_depth_stencil)) {
		static std::atomic<uint32_t> reports {0};
		if (reports.fetch_add(1, std::memory_order_relaxed) < 4)
			FSR_PRINT(
			    "FSR upscaling: the game's images have several levels or layers (or a stencil "
			    "this GPU cannot lay out apart); stretched instead\n");
		return false;
	}
	const vk::Extent2D render = inputs.color.extent, output = inputs.output.extent;
	const bool infinite = !(inputs.far_plane > inputs.near_plane) || inputs.far_plane >= 1.0e5f;
	uint32_t   flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED |
	                   FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
	if (infinite) flags |= FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
	if (g_debug) flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
	if (!EnsureUpscaleContext(render, output, flags)) {
		g_upscale_ready = false; // (stretched from now on: no retry every frame)
		return false;
	}
	static const auto      jitter_sign = SignPair("KYTY_FSR_JITTER");
	static const auto      motion_sign = SignPair("KYTY_FSR_MV");
	ffxDispatchDescUpscale dispatch {};
	dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
	dispatch.commandList = static_cast<void*>(static_cast<VkCommandBuffer>(command));
	// (Inputs in shader-read layouts, the output general: renderSuperResolution.cpp leaves them so
	// for FSR.)
	dispatch.color = Resource(inputs.color, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, false);
	dispatch.depth = Resource(inputs.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, false);
	dispatch.motionVectors =
	    Resource(inputs.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, false);
	dispatch.output = Resource(inputs.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, true);
	// The packet's jitter is in render pixels, its motion scale takes the vectors to render pixels
	// towards the previous frame: FSR's own units (super-resolution-upscaler.md, "Providing motion
	// vectors").
	dispatch.jitterOffset = {inputs.jitter_x * jitter_sign[0], inputs.jitter_y * jitter_sign[1]};
	dispatch.motionVectorScale = {inputs.motion_scale_x * motion_sign[0],
	                              inputs.motion_scale_y * motion_sign[1]};
	dispatch.renderSize        = {render.width, render.height};
	dispatch.upscaleSize       = {output.width, output.height};
	const float sharpness      = Sharpness();
	dispatch.enableSharpening  = sharpness > 0.0f;
	dispatch.sharpness         = sharpness;
	dispatch.frameTimeDelta    = MillisecondsSince(g_upscale_last);
	dispatch.preExposure       = 1.0f;
	dispatch.reset             = inputs.reset;
	dispatch.cameraNear        = inputs.near_plane > 0.0f ? inputs.near_plane : 0.1f;
	dispatch.cameraFar         = infinite ? 1.0e6f : inputs.far_plane;
	dispatch.cameraFovAngleVertical =
	    inputs.view_to_clip[5] != 0.0f ? 2.0f * std::atan(1.0f / std::fabs(inputs.view_to_clip[5]))
	                                   : 1.0f;
	dispatch.viewSpaceToMetersFactor = 1.0f;
	dispatch.flags                   = 0;
	if (g_verbose && g_upscale_count < 2) {
		FSR_PRINT("FSR upscaling: color %ux%u fmt %d, depth fmt %d, motion fmt %d, output %ux%u "
		          "fmt %d; jitter %g %g, motion "
		          "scale %g %g, near %g far %g fov %g reset %d\n",
		          render.width, render.height, static_cast<int>(inputs.color.format),
		          static_cast<int>(inputs.depth.format), static_cast<int>(inputs.motion.format),
		          output.width, output.height, static_cast<int>(inputs.output.format),
		          dispatch.jitterOffset.x, dispatch.jitterOffset.y, dispatch.motionVectorScale.x,
		          dispatch.motionVectorScale.y, dispatch.cameraNear, dispatch.cameraFar,
		          dispatch.cameraFovAngleVertical, inputs.reset ? 1 : 0);
	}
	++g_upscale_count;
	if (const auto code = g_api.dispatch(&g_upscale.context, &dispatch.header); !Ok(code)) {
		static std::atomic<uint32_t> reports {0};
		if (reports.fetch_add(1, std::memory_order_relaxed) < 8)
			FSR_PRINT("FSR upscaling: the dispatch failed (%u)\n", code);
		return false;
	}
	return true;
}

bool FrameGenReady() {
	return g_fg_ready.load(std::memory_order_relaxed);
}

vk::Result CreateSwapchain(GraphicContext& graphics, const vk::SwapchainCreateInfoKHR& create_info,
                           vk::SwapchainKHR* swapchain) {
	EXIT_IF(g_swapchain_context != nullptr);
	ffxCreateContextDescFrameGenerationSwapChainVK desc {};
	desc.header.type             = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
	desc.physicalDevice          = static_cast<VkPhysicalDevice>(graphics.physical_device);
	desc.device                  = static_cast<VkDevice>(graphics.device);
	VkSwapchainKHR made          = VK_NULL_HANDLE;
	desc.swapchain               = &made;
	desc.allocator               = nullptr;
	desc.createInfo              = static_cast<VkSwapchainCreateInfoKHR>(create_info);
	desc.createInfo.oldSwapchain = VK_NULL_HANDLE;
	// The game queue is the emulator's own (its submissions take its lock); the others are
	// FidelityFX's alone.
	// (No async compute queue: allowed, and the work stays off it anyway.)
	desc.gameQueue          = QueueInfo(graphics.queue, graphics.queue_family, SubmitGame);
	desc.asyncComputeQueue  = QueueInfo(nullptr, g_present_slot.family, nullptr);
	desc.presentQueue       = QueueInfo(g_present_queue, g_present_slot.family, nullptr);
	desc.imageAcquireQueue  = QueueInfo(g_acquire_queue, g_acquire_slot.family, nullptr);
	ffxReturnCode_t created = FFX_API_RETURN_ERROR;
	{
		const QueueLocks locks; // (its failure path waits for the queues)
		created = g_api.create(&g_swapchain_context, &desc.header, nullptr);
	}
	if (!Ok(created) || g_swapchain_context == nullptr || made == VK_NULL_HANDLE) {
		g_swapchain_context = nullptr;
		g_fg_ready          = false;
		FSR_PRINT("FSR frame generation: FidelityFX's swapchain could not be made; frame "
		          "generation is off\n");
		return vk::Result::eErrorInitializationFailed;
	}
	g_functions             = {};
	g_functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
	if (!Ok(g_api.query(&g_swapchain_context, &g_functions.header)) ||
	    g_functions.pOutAcquireNextImageKHR == nullptr ||
	    g_functions.pOutQueuePresentKHR == nullptr ||
	    g_functions.pOutGetSwapchainImagesKHR == nullptr) {
		{
			const QueueLocks locks;
			(void)g_api.destroy(&g_swapchain_context, nullptr);
		}
		g_swapchain_context = nullptr;
		g_fg_ready          = false;
		FSR_PRINT("FSR frame generation: FidelityFX's swapchain offers no functions; frame "
		          "generation is off\n");
		return vk::Result::eErrorInitializationFailed;
	}
	g_swapchain = made;
	*swapchain  = vk::SwapchainKHR(made);
	g_hdr_now.store(create_info.imageColorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT,
	                std::memory_order_relaxed);
	g_count_frames = g_count_presents = 0;
	return vk::Result::eSuccess;
}

void DestroySwapchain(GraphicContext& graphics, vk::SwapchainKHR swapchain) {
	(void)graphics;
	(void)swapchain;
	DestroyFrameGenContext();
	if (g_swapchain_context != nullptr) {
		// (The context owns its swapchain: both go.)
		const QueueLocks locks;
		(void)g_api.destroy(&g_swapchain_context, nullptr);
		g_swapchain_context = nullptr;
	}
	g_swapchain = VK_NULL_HANDLE;
	g_functions = {};
}

vk::Result GetSwapchainImages(GraphicContext& graphics, vk::SwapchainKHR swapchain, uint32_t* count,
                              vk::Image* images) {
	return static_cast<vk::Result>(g_functions.pOutGetSwapchainImagesKHR(
	    static_cast<VkDevice>(graphics.device), static_cast<VkSwapchainKHR>(swapchain), count,
	    reinterpret_cast<VkImage*>(images)));
}

vk::Result AcquireNextImage(GraphicContext& graphics, vk::SwapchainKHR swapchain, uint64_t timeout,
                            vk::Semaphore semaphore, uint32_t* index) {
	return static_cast<vk::Result>(g_functions.pOutAcquireNextImageKHR(
	    static_cast<VkDevice>(graphics.device), static_cast<VkSwapchainKHR>(swapchain), timeout,
	    static_cast<VkSemaphore>(semaphore), VK_NULL_HANDLE, index));
}

vk::Result QueuePresent(GraphicContext& graphics, const vk::PresentInfoKHR& present) {
	// (On the game queue, without its lock: FidelityFX submits there through SubmitGame, which
	// takes it.)
	const VkPresentInfoKHR info   = present;
	const auto             result = static_cast<vk::Result>(
	    g_functions.pOutQueuePresentKHR(static_cast<VkQueue>(graphics.queue), &info));
	// Frames shown per rendered frame, about once a second: the swapchain's presents against ours.
	++g_count_frames;
	if (g_functions.pOutGetLastPresentCountFFXAPI != nullptr && g_count_frames % 30 == 0) {
		const uint64_t presents = g_functions.pOutGetLastPresentCountFFXAPI(g_swapchain);
		if (g_count_presents != 0 && presents > g_count_presents) {
			const double ratio = static_cast<double>(presents - g_count_presents) / 30.0;
			g_presented.store(std::clamp(static_cast<uint32_t>(std::lround(ratio)), 1u, 2u),
			                  std::memory_order_relaxed);
		}
		g_count_presents = presents;
	}
	return result;
}

void BeginPresent(const FrameInputs* inputs, vk::Extent2D backbuffer, vk::Format format, bool hdr,
                  bool overlay) {
	g_pending = {};
	if (!FrameGenReady() || g_swapchain_context == nullptr) return;
	// (A depth with stencil needs separate depth-stencil layouts: frames without it are not
	// prepared.)
	const bool usable = inputs != nullptr && inputs->valid && inputs->camera.valid &&
	                    (!HasStencil(inputs->depth.format) || g_separate_depth_stencil);
	g_run_with        = usable ? g_run_with + 1 : 0;
	g_run_without     = usable ? 0 : g_run_without + 1;
	const bool wanted = UserEnabled() && !overlay;
	const bool mode_on =
	    wanted && (g_mode_on ? g_run_without < TurnOffAfter : g_run_with >= TurnOnAfter);
	g_hdr_now.store(hdr, std::memory_order_relaxed);
	if (usable) {
		const FgShape shape {backbuffer, inputs->render_extent, format, hdr, HudlessOn()};
		if (!EnsureFrameGenContext(shape)) return;
	}
	if (g_fg_context == nullptr)
		return; // (no frame with inputs yet: the swapchain shows frames as they come)
	if (mode_on != g_mode_on) {
		FSR_PRINT("FSR frame generation: %s\n", mode_on ? "on"
		                                                : (!UserEnabled() ? "off (hotkey)"
		                                                   : overlay      ? "paused (menu)"
		                                                                  : "paused (no 3D view)"));
		if (mode_on) g_reset_next.store(true, std::memory_order_relaxed);
		g_mode_on = mode_on;
	}
	g_pending = {++g_frame_id, MillisecondsSince(g_fg_last), usable, mode_on, overlay, true};
}

void ConfigurePresent(const FrameInputs* inputs, vk::Rect2D region) {
	if (!g_pending.valid || g_fg_context == nullptr) return;
	const auto     pending     = std::exchange(g_pending, {});
	const uint32_t debug_flags = g_debug ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES |
	                                           FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES
	                                     : 0u;
	ffxConfigureDescFrameGeneration config {};
	config.header.type                        = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
	config.swapChain                          = reinterpret_cast<void*>(g_swapchain);
	config.presentCallback                    = nullptr;
	config.presentCallbackUserContext         = nullptr;
	config.frameGenerationCallback            = GenerateFrame;
	config.frameGenerationCallbackUserContext = &g_fg_context;
	config.frameGenerationEnabled             = pending.mode_on;
	config.allowAsyncWorkloads                = false;
	config.flags                              = debug_flags;
	config.onlyPresentGenerated               = false;
	config.generationRect                     = {region.offset.x, region.offset.y,
	                                             static_cast<int32_t>(region.extent.width),
	                                             static_cast<int32_t>(region.extent.height)};
	config.frameID                            = pending.frame_id;
	if (pending.prepare && inputs != nullptr && g_fg_shape.hudless && inputs->hudless.has_display)
		config.HUDLessColor =
		    Resource(inputs->hudless.display, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	ffxReturnCode_t code = FFX_API_RETURN_ERROR;
	if (g_configured_enabled != static_cast<int>(pending.mode_on)) {
		// (Turning generation on or off waits for the presents and the queues: no renderer lock is
		// held here, the queues' locks are taken.)
		const QueueLocks locks;
		code = g_api.configure(&g_fg_context, &config.header);
	} else {
		code = g_api.configure(&g_fg_context, &config.header);
	}
	if (!Ok(code)) {
		static std::atomic<uint32_t> reports {0};
		if (reports.fetch_add(1, std::memory_order_relaxed) < 8)
			FSR_PRINT("FSR frame generation: configure failed (%u)\n", code);
		return;
	}
	g_configured_enabled = static_cast<int>(pending.mode_on);
}

void RecordPresent(vk::CommandBuffer command, const FrameInputs* inputs) {
	if (!g_pending.valid || !g_pending.prepare || inputs == nullptr || g_fg_context == nullptr)
		return;
	const auto&    pending     = g_pending;
	const uint32_t debug_flags = g_debug ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES |
	                                           FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES
	                                     : 0u;
	// The frame's preparation: its depth and motion vectors (copies at the flip, shader-read), its
	// camera.
	const auto  camera = Decompose(inputs->camera.view_proj);
	const float width  = static_cast<float>(inputs->render_extent.width);
	const float height = static_cast<float>(inputs->render_extent.height);
	// The game's vectors point along the motion in UV units (frame-gen.cpp: its TAA reads the
	// history at uv - mv); FSR's go towards the previous position in pixels.
	// KYTY_FRAMEGEN_MV_SIGN=1 keeps the game's sign (a test).
	static const float mv_sign = [] {
		const char* sign = std::getenv("KYTY_FRAMEGEN_MV_SIGN");
		return sign != nullptr && std::atof(sign) > 0 ? 1.0f : -1.0f;
	}();
	ffxDispatchDescFrameGenerationPrepareCameraInfo camera_info {};
	camera_info.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
	for (int i = 0; i < 3; ++i) {
		camera_info.cameraPosition[i] = camera.position[i];
		camera_info.cameraUp[i]       = camera.up[i];
		camera_info.cameraRight[i]    = camera.right[i];
		camera_info.cameraForward[i]  = camera.forward[i];
	}
	ffxDispatchDescFrameGenerationPrepare prepare {};
	prepare.header.type       = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
	prepare.header.pNext      = camera.valid ? &camera_info.header : nullptr;
	prepare.frameID           = pending.frame_id;
	prepare.flags             = debug_flags;
	prepare.commandList       = static_cast<void*>(static_cast<VkCommandBuffer>(command));
	prepare.renderSize        = {inputs->render_extent.width, inputs->render_extent.height};
	prepare.jitterOffset      = {inputs->camera.jitter_ndc[0] * width * 0.5f,
	                             -inputs->camera.jitter_ndc[1] * height * 0.5f};
	prepare.motionVectorScale = {mv_sign * width, mv_sign * height};
	prepare.frameTimeDelta    = pending.frame_ms;
	prepare.cameraNear        = camera.valid && camera.z_near > 0.0f ? camera.z_near : 0.1f;
	prepare.cameraFar         = 1.0e6f;
	prepare.cameraFovAngleVertical =
	    camera.valid ? 2.0f * std::atan(1.0f / std::fabs(camera.sy)) : 1.0f;
	prepare.viewSpaceToMetersFactor = 1.0f;
	prepare.depth         = Resource(inputs->depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	prepare.motionVectors = Resource(inputs->motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	if (const auto code = g_api.dispatch(&g_fg_context, &prepare.header); !Ok(code)) {
		static std::atomic<uint32_t> reports {0};
		if (reports.fetch_add(1, std::memory_order_relaxed) < 8)
			FSR_PRINT("FSR frame generation: prepare failed (%u)\n", code);
		return;
	}
	g_last_prepared.store(pending.frame_id, std::memory_order_relaxed);
}

uint32_t PresentedFrames() {
	return g_mode_on ? g_presented.load(std::memory_order_relaxed) : 1u;
}

void NoteDroppedFrame() {
	g_reset_next.store(true, std::memory_order_relaxed);
}

float Sharpness() {
	return static_cast<float>(g_sharpness_milli.load(std::memory_order_relaxed)) / 1000.0f;
}

void SetSharpness(float value) {
	const float clamped = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.25f;
	g_sharpness_milli.store(static_cast<uint32_t>(std::lround(clamped * 1000.0f)),
	                        std::memory_order_relaxed);
}

} // namespace Libs::Graphics::FrameGen::Fsr
