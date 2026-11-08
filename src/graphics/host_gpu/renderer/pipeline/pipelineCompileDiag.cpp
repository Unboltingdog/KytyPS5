#include "graphics/host_gpu/renderer/pipeline/pipelineCompileDiag.h"

#include "common/threads.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include <xxhash.h>

namespace Libs::Graphics::PipelineCompileDiag {

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t MaxSlots = 128;

struct StageRecord {
	const char*          name = "unknown";
	uint64_t             hash = 0;
	uint64_t             guest_address = 0;
	std::vector<uint32_t> spirv;
};

struct Slot {
	std::atomic<Record*> active {nullptr};
};

[[nodiscard]] int Mode() {
	static const int mode = [] {
		const char* value = std::getenv("KYTY_DIAG_PIPELINE_COMPILE");
		return value != nullptr && (value[0] == '1' || value[0] == '2') ? value[0] - '0' : 0;
	}();
	return mode;
}

[[nodiscard]] int OptimizationMode() {
	static const int mode = [] {
		const char* value = std::getenv("KYTY_FIX_PIPELINE_DISABLE_OPT");
		return value != nullptr && (value[0] == '1' || value[0] == '2') ? value[0] - '0' : 0;
	}();
	return mode;
}

[[nodiscard]] const std::unordered_set<uint64_t>& OptimizationHashes() {
	static const std::unordered_set<uint64_t> hashes = [] {
		std::unordered_set<uint64_t> result;
		const char* cursor = std::getenv("KYTY_FIX_PIPELINE_DISABLE_OPT_HASHES");
		while (cursor != nullptr && *cursor != '\0') {
			char* end = nullptr;
			const auto hash = std::strtoull(cursor, &end, 16);
			if (end == cursor) break;
			result.insert(hash);
			cursor = *end == ',' ? end + 1 : end;
			if (*end != '\0' && *end != ',') break;
		}
		return result;
	}();
	return hashes;
}

[[nodiscard]] std::chrono::seconds SlowAfter() {
	static const std::chrono::seconds seconds = [] {
		const char* value = std::getenv("KYTY_DIAG_PIPELINE_COMPILE_SLOW_SEC");
		const auto parsed = value != nullptr ? std::strtoul(value, nullptr, 10) : 5ul;
		return std::chrono::seconds(parsed != 0 ? parsed : 5ul);
	}();
	return seconds;
}

[[nodiscard]] std::array<Slot, MaxSlots>& Slots() {
	static std::array<Slot, MaxSlots> slots {};
	return slots;
}

[[nodiscard]] uint32_t CurrentSlot() {
	static std::atomic_uint next {0};
	thread_local const uint32_t slot = next.fetch_add(1, std::memory_order_relaxed);
	return slot;
}

[[nodiscard]] uint64_t ElapsedMs(const Record& record, Clock::time_point now);

void PrintStages(const Record& record, const char* prefix, uint64_t elapsed_ms, bool write_spirv);

void Watch();
void StartWatcher();

} // namespace

struct Record {
	Kind                          kind = Kind::Compute;
	uint64_t                      key_hash = 0;
	Clock::time_point             start {};
	std::vector<StageRecord>      stages;
	std::atomic_bool              slow_reported {false};
};

namespace {

[[nodiscard]] uint64_t ElapsedMs(const Record& record, Clock::time_point now) {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - record.start).count());
}

void PrintStages(const Record& record, const char* prefix, uint64_t elapsed_ms, bool write_spirv) {
	const char* kind = record.kind == Kind::Compute ? "compute" : "graphics";
	for (const auto& stage: record.stages) {
		std::fprintf(stderr, "%s%s kind=%s stage=%s hash=0x%016" PRIx64 " spirv_hash=0x%016" PRIx64
		                     " ms=%" PRIu64 " guest=0x%016" PRIx64,
		             prefix, write_spirv ? "" : " pipeline_in_flight", kind, stage.name, record.key_hash, stage.hash,
		             elapsed_ms, stage.guest_address);
		if (write_spirv && !stage.spirv.empty()) {
			char name[160];
			std::snprintf(name, sizeof(name), "_Logs/slow-%016" PRIx64 "-%s.spv", stage.hash, stage.name);
			std::error_code error;
			std::filesystem::create_directories("_Logs", error);
			if (std::FILE* file = std::fopen(name, "wb"); file != nullptr) {
				std::fwrite(stage.spirv.data(), sizeof(uint32_t), stage.spirv.size(), file);
				std::fclose(file);
				std::fprintf(stderr, " file=%s", name);
			} else {
				std::fprintf(stderr, " file_error=1");
			}
		}
		std::fprintf(stderr, "\n");
	}
	std::fflush(stderr);
}

void Watch() {
	Common::Thread::SetCurrentName("Thread_PipelineCompile");
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		const auto now = Clock::now();
		for (auto& slot: Slots()) {
			auto* record = slot.active.load(std::memory_order_acquire);
			if (record == nullptr || now - record->start < SlowAfter() ||
			    record->slow_reported.exchange(true, std::memory_order_relaxed)) {
				continue;
			}
			PrintStages(*record, "PIPELINE_COMPILE_SLOW", ElapsedMs(*record, now), true);
		}
	}
}

void StartWatcher() {
	static std::once_flag once;
	std::call_once(once, [] { std::thread(Watch).detach(); });
}

} // namespace

bool Enabled() {
	return Mode() != 0;
}

uint64_t SpirvHash(std::span<const uint32_t> spirv) {
	return spirv.empty() ? 0 : XXH3_64bits(spirv.data(), spirv.size_bytes());
}

void PrepareShader(ShaderRecompiler::IR::CompiledShaderInfo& info, std::span<const uint32_t> spirv,
                   uint64_t guest_address) {
	info.diagnostic_spirv_hash = SpirvHash(spirv);
	info.diagnostic_guest_address = guest_address;
	if (!Enabled() && OptimizationMode() == 0) return;
	info.diagnostic_spirv = std::make_shared<const std::vector<uint32_t>>(spirv.begin(), spirv.end());
}

Stage ShaderStage(const char* name, const ShaderRecompiler::IR::CompiledShaderInfo* info) {
	if (info == nullptr || info->diagnostic_spirv == nullptr) return {.name = name};
	return {
	    .name = name,
	    .spirv = *info->diagnostic_spirv,
	    .guest_address = info->diagnostic_guest_address,
	};
}

bool DisableOptimization(Kind kind, std::span<const Stage> stages) {
	const int mode = OptimizationMode();
	if (mode == 0 || (kind == Kind::Graphics && mode != 2)) return false;
	const auto& hashes = OptimizationHashes();
	if (hashes.empty()) return true;
	return std::ranges::any_of(stages, [](const Stage& stage) {
		return OptimizationHashes().contains(SpirvHash(stage.spirv));
	});
}

Compile::Compile(Kind kind, uint64_t key_hash, std::span<const Stage> stages) {
	if (!Enabled()) return;
	m_slot = CurrentSlot();
	if (m_slot >= MaxSlots) return;
	StartWatcher();
	// Published records stay alive until process exit so a watcher never dereferences a completed compile's storage.
	auto* record = new Record {
	    .kind = kind,
	    .key_hash = key_hash,
	    .start = Clock::now(),
	};
	record->stages.reserve(stages.size());
	for (const auto& stage: stages) {
		record->stages.push_back({
		    .name = stage.name,
		    .hash = SpirvHash(stage.spirv),
		    .guest_address = stage.guest_address,
		    .spirv = {stage.spirv.begin(), stage.spirv.end()},
		});
	}
	m_record = record;
	Slots()[m_slot].active.store(record, std::memory_order_release);
}

void Compile::End(vk::Result result) {
	if (m_record == nullptr) return;
	const auto elapsed_ms = ElapsedMs(*m_record, Clock::now());
	Slots()[m_slot].active.store(nullptr, std::memory_order_release);
	if (Mode() == 2 || elapsed_ms >= 250) {
		for (const auto& stage: m_record->stages) {
			std::fprintf(stderr,
			             "PIPELINE_COMPILE kind=%s stage=%s hash=0x%016" PRIx64 " spirv_hash=0x%016" PRIx64
			             " spirv_bytes=%zu ms=%" PRIu64 " guest=0x%016" PRIx64 " result=%s\n",
			             m_record->kind == Kind::Compute ? "compute" : "graphics", stage.name, m_record->key_hash,
			             stage.hash, stage.spirv.size() * sizeof(uint32_t), elapsed_ms, stage.guest_address,
			             vk::to_string(result).c_str());
		}
		std::fflush(stderr);
	}
	m_record = nullptr;
}

void DumpInflight(const char* prefix) {
	const auto now = Clock::now();
	for (auto& slot: Slots()) {
		if (const auto* record = slot.active.load(std::memory_order_acquire); record != nullptr) {
			PrintStages(*record, prefix, ElapsedMs(*record, now), false);
		}
	}
}

} // namespace Libs::Graphics::PipelineCompileDiag
