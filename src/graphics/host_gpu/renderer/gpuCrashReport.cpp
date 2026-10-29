#include "graphics/host_gpu/renderer/gpuCrashReport.h"

#include "common/logging/log.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Libs::Graphics::GpuCrashReport {

namespace {

struct Note {
	uint64_t   hash  = 0;
	uint64_t   tick  = 0;
	ShaderType stage = ShaderType::Unknown;
};

constexpr size_t RingSize = 4096;

std::atomic<const MasterSemaphore*> g_master {nullptr};
std::mutex                          g_mutex;
std::array<Note, RingSize>          g_ring {};
size_t                              g_next  = 0;
size_t                              g_count = 0;

const char* StageName(ShaderType stage) {
	switch (stage) {
		case ShaderType::Compute: return "CS";
		case ShaderType::Vertex: return "VS";
		case ShaderType::Pixel: return "PS";
		case ShaderType::Mesh: return "MS";
		case ShaderType::Local: return "LS";
		case ShaderType::TessellationControl: return "HS";
		case ShaderType::TessellationEvaluation: return "TES";
		default: return "?";
	}
}

} // namespace

void SetMaster(const MasterSemaphore* master) {
	g_master.store(master, std::memory_order_release);
}

void NoteShader(uint64_t hash, ShaderType stage) {
	const auto* master = g_master.load(std::memory_order_acquire);
	const auto  tick   = master != nullptr ? master->CurrentTick() : 0;
	std::scoped_lock lock(g_mutex);
	auto&            last = g_ring[(g_next + RingSize - 1) % RingSize];
	// (A shader used again in the same command buffer is noted once.)
	if (g_count != 0 && last.hash == hash && last.tick == tick && last.stage == stage) return;
	g_ring[g_next] = {hash, tick, stage};
	g_next         = (g_next + 1) % RingSize;
	g_count        = std::min(g_count + 1, RingSize);
}

void Report(GraphicContext& graphics, vk::Result result, uint64_t completed) {
	static std::atomic_bool reported {false};
	if (reported.exchange(true)) return;
	std::string text = "\n===== GPU crash report =====\n";
	char        line[512];
	std::snprintf(line, sizeof(line), "Vulkan result: %s (%d); last finished tick %" PRIu64 "\n",
	              vk::to_string(result).c_str(), static_cast<int>(result), completed);
	text += line;

	if (graphics.device_fault_enabled) {
		vk::DeviceFaultCountsEXT counts {};
		auto                     fault = graphics.device.getFaultInfoEXT(&counts, nullptr);
		std::vector<vk::DeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
		std::vector<vk::DeviceFaultVendorInfoEXT>  vendors(counts.vendorInfoCount);
		vk::DeviceFaultInfoEXT                     info {};
		info.pAddressInfos     = addresses.empty() ? nullptr : addresses.data();
		info.pVendorInfos      = vendors.empty() ? nullptr : vendors.data();
		counts.vendorBinarySize = 0;
		if (fault == vk::Result::eSuccess || fault == vk::Result::eIncomplete) {
			fault = graphics.device.getFaultInfoEXT(&counts, &info);
		}
		std::snprintf(line, sizeof(line), "Device fault (%s): %s\n", vk::to_string(fault).c_str(),
		              info.description.data());
		text += line;
		for (uint32_t i = 0; i < counts.addressInfoCount && i < addresses.size(); i++) {
			const auto& address = addresses[i];
			std::snprintf(line, sizeof(line), "  address: %s 0x%016" PRIx64 " (precision 0x%" PRIx64 ")\n",
			              vk::to_string(address.addressType).c_str(),
			              static_cast<uint64_t>(address.reportedAddress),
			              static_cast<uint64_t>(address.addressPrecision));
			text += line;
		}
		for (uint32_t i = 0; i < counts.vendorInfoCount && i < vendors.size(); i++) {
			const auto& vendor = vendors[i];
			std::snprintf(line, sizeof(line), "  vendor: %s code 0x%" PRIx64 " data 0x%" PRIx64 "\n",
			              vendor.description.data(), static_cast<uint64_t>(vendor.vendorFaultCode),
			              static_cast<uint64_t>(vendor.vendorFaultData));
			text += line;
		}
	} else {
		text += "Device fault: VK_EXT_device_fault is not available on this driver\n";
	}

	{
		std::scoped_lock lock(g_mutex);
		struct Seen {
			uint64_t   first_tick = 0, last_tick = 0;
			uint32_t   uses = 0;
			ShaderType stage = ShaderType::Unknown;
		};
		std::map<uint64_t, Seen> pending;
		std::vector<Note>        recent;
		for (size_t i = 0; i < g_count; i++) {
			const auto& note = g_ring[(g_next + RingSize - g_count + i) % RingSize];
			if (note.tick > completed) {
				auto& seen = pending[note.hash];
				if (seen.uses++ == 0) seen.first_tick = note.tick;
				seen.last_tick = note.tick;
				seen.stage     = note.stage;
			}
			recent.push_back(note);
		}
		std::snprintf(line, sizeof(line), "Shaders in unfinished command buffers (tick > %" PRIu64 "): %zu\n",
		              completed, pending.size());
		text += line;
		for (const auto& [hash, seen]: pending) {
			std::snprintf(line, sizeof(line), "  %s 0x%016" PRIx64 " ticks %" PRIu64 "-%" PRIu64 " (%u)\n",
			              StageName(seen.stage), hash, seen.first_tick, seen.last_tick, seen.uses);
			text += line;
		}
		text += "Last shaders handed out (oldest first):\n";
		const size_t from = recent.size() > 48 ? recent.size() - 48 : 0;
		for (size_t i = from; i < recent.size(); i++) {
			std::snprintf(line, sizeof(line), "  tick %" PRIu64 " %s 0x%016" PRIx64 "\n", recent[i].tick,
			              StageName(recent[i].stage), recent[i].hash);
			text += line;
		}
	}
	text += "===== end of GPU crash report =====\n";

	std::fputs(text.c_str(), stdout);
	std::fflush(stdout);
	LOGF("%s", text.c_str());
	if (auto* file = std::fopen("gpu-crash.txt", "w")) {
		std::fputs(text.c_str(), file);
		std::fclose(file);
	}
}

} // namespace Libs::Graphics::GpuCrashReport
