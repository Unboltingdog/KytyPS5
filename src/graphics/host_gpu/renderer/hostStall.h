#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_HOSTSTALL_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_HOSTSTALL_H_

#include "common/common.h"

#include <memory>

namespace Libs::Graphics {

class GuestGpu;
class RenderContext;

class HostStallWatchdog {
public:
	HostStallWatchdog(GuestGpu& gpu, RenderContext& renderer);
	~HostStallWatchdog();
	KYTY_CLASS_NO_COPY(HostStallWatchdog);

	void Stop();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_HOSTSTALL_H_
