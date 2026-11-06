#include "graphics/host_gpu/hostImportProbe.h"

#include "common/diagLog.h"
#include "graphics/host_gpu/graphicContext.h"

#include <atomic>
#include <cstdlib>
#include <fmt/format.h>
#include <optional>
#include <string>

namespace Libs::Graphics::HostImportProbe {
namespace {

void Write(const std::string& line) {
	Common::DiagLog::Write(line.c_str());
}

} // namespace

bool Enabled() noexcept {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_HOST_IMPORT_PROBE");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

void LogDeviceCapabilities(const GraphicContext& graphics) {
	if (!Enabled()) return;
	Write(fmt::format("host-import probe device supported={} enabled={} "
	                  "minImportedHostPointerAlignment=0x{:x}\n",
	                  graphics.host_import_supported, graphics.host_import_enabled,
	                  graphics.min_imported_host_pointer_alignment));
}

void ProbeFirstDirectMapping(GraphicContext& graphics, uint64_t mapping_address,
                             uint64_t mapping_size) {
	static std::atomic_bool attempted = false;
	if (mapping_size < ProbeWindowSize || !Enabled() ||
	    attempted.exchange(true, std::memory_order_acq_rel)) {
		return;
	}
	Write(fmt::format("host-import probe mapping address=0x{:x} size=0x{:x}\n", mapping_address,
	                  mapping_size));
	if (!graphics.host_import_enabled) {
		Write("host-import probe skipped: VK_EXT_external_memory_host is not enabled\n");
		return;
	}
	const auto window =
	    SelectWindow(mapping_address, mapping_size, graphics.min_imported_host_pointer_alignment);
	if (!window.has_value()) {
		Write(fmt::format("host-import probe window failed: no aligned 64 MiB window, "
		                  "alignment=0x{:x}\n",
		                  graphics.min_imported_host_pointer_alignment));
		return;
	}
	Write(fmt::format("host-import probe window address=0x{:x} size=0x{:x} alignment=0x{:x}\n",
	                  window->address, window->size, graphics.min_imported_host_pointer_alignment));
	if (VULKAN_HPP_DEFAULT_DISPATCHER.vkGetMemoryHostPointerPropertiesEXT == nullptr) {
		Write("host-import probe properties failed: vkGetMemoryHostPointerPropertiesEXT is null\n");
		return;
	}

	constexpr auto handle_type = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT;
	const auto* host_pointer   = reinterpret_cast<const void*>(window->address);
	vk::MemoryHostPointerPropertiesEXT host_properties {};
	auto result = graphics.device.getMemoryHostPointerPropertiesEXT(handle_type, host_pointer,
	                                                                &host_properties);
	Write(fmt::format("host-import probe properties result={} memoryTypeBits=0x{:08x}\n",
	                  vk::to_string(result), host_properties.memoryTypeBits));
	if (result != vk::Result::eSuccess) return;

	const auto& memory = graphics.physical_device_memory_properties;
	for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
		if ((host_properties.memoryTypeBits & (1u << index)) == 0) continue;
		const auto& type = memory.memoryTypes[index];
		const auto& heap = memory.memoryHeaps[type.heapIndex];
		Write(fmt::format("host-import probe type={} heap={} heapFlags={} flags={} heapSize=0x{:x}\n",
		                  index, type.heapIndex, vk::to_string(heap.flags),
		                  vk::to_string(type.propertyFlags), heap.size));
	}

	vk::BufferCreateInfo buffer_info {};
	buffer_info.size        = window->size;
	buffer_info.usage       = vk::BufferUsageFlagBits::eStorageBuffer |
	                        vk::BufferUsageFlagBits::eTransferSrc |
	                        vk::BufferUsageFlagBits::eTransferDst;
	buffer_info.sharingMode = vk::SharingMode::eExclusive;
	vk::Buffer buffer       = nullptr;
	result = graphics.device.createBuffer(&buffer_info, nullptr, &buffer);
	Write(fmt::format("host-import probe create-buffer result={}\n", vk::to_string(result)));
	if (result != vk::Result::eSuccess) return;

	vk::MemoryRequirements requirements {};
	graphics.device.getBufferMemoryRequirements(buffer, &requirements);
	const auto compatible_bits = host_properties.memoryTypeBits & requirements.memoryTypeBits;
	Write(fmt::format("host-import probe requirements size=0x{:x} align=0x{:x} "
	                  "memoryTypeBits=0x{:08x} compatibleBits=0x{:08x}\n",
	                  requirements.size, requirements.alignment, requirements.memoryTypeBits,
	                  compatible_bits));
	std::optional<uint32_t> fallback_type;
	std::optional<uint32_t> selected_type;
	for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
		if ((compatible_bits & (1u << index)) == 0) continue;
		if (!fallback_type.has_value()) fallback_type = index;
		constexpr auto coherent = vk::MemoryPropertyFlagBits::eHostVisible |
		                          vk::MemoryPropertyFlagBits::eHostCoherent;
		if ((memory.memoryTypes[index].propertyFlags & coherent) == coherent) {
			selected_type = index;
			break;
		}
	}
	if (!selected_type.has_value()) selected_type = fallback_type;
	if (!selected_type.has_value()) {
		Write("host-import probe allocate skipped: no compatible memory type\n");
		graphics.device.destroyBuffer(buffer, nullptr);
		return;
	}

	vk::ImportMemoryHostPointerInfoEXT import {};
	import.handleType   = handle_type;
	import.pHostPointer = const_cast<void*>(host_pointer);
	vk::MemoryAllocateInfo allocate {};
	allocate.pNext           = &import;
	allocate.allocationSize  = requirements.size;
	allocate.memoryTypeIndex = *selected_type;
	vk::DeviceMemory imported = nullptr;
	result = graphics.device.allocateMemory(&allocate, nullptr, &imported);
	Write(fmt::format("host-import probe allocate result={} memoryType={}\n", vk::to_string(result),
	                  *selected_type));
	if (result == vk::Result::eSuccess) {
		result = graphics.device.bindBufferMemory(buffer, imported, 0);
		Write(fmt::format("host-import probe bind result={}\n", vk::to_string(result)));
	}
	graphics.device.destroyBuffer(buffer, nullptr);
	if (imported != nullptr) graphics.device.freeMemory(imported, nullptr);
	Write("host-import probe cleanup complete\n");
}

} // namespace Libs::Graphics::HostImportProbe
