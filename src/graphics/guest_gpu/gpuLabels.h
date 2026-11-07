#ifndef KYTY_GRAPHICS_GUEST_GPU_GPU_LABELS_H_
#define KYTY_GRAPHICS_GUEST_GPU_GPU_LABELS_H_

#include <cstdint>

namespace Libs::Graphics::GpuLabels {

bool Enabled();
void Write(const char* packet, uint32_t queue, uint64_t address, uint64_t value, uint32_t size);
void WriteRange(const char* packet, uint32_t queue, uint64_t address, const volatile void* value, uint64_t size);
void Wait(const char* packet, uint32_t queue, uint32_t function, uint64_t address, uint64_t reference,
          uint64_t mask, uint64_t value, bool met);

} // namespace Libs::Graphics::GpuLabels

#endif
