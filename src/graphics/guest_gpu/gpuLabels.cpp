#include "graphics/guest_gpu/gpuLabels.h"

#include "common/guestWaits.h"
#include "graphics/host_gpu/renderer/debugSample.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Libs::Graphics::GpuLabels {
namespace {

using Clock = std::chrono::steady_clock;

struct WriteRecord {
	uint64_t    address = 0;
	uint64_t    first_frame = 0;
	uint64_t    last_frame = 0;
	uint64_t    count = 0;
	uint64_t    last_value = 0;
	uint64_t    size = 0;
	uint64_t    sequence = 0;
	const char* packet = nullptr;
	uint32_t    queue = 0;
};

struct WaitRecord {
	uint64_t    first_frame = 0;
	uint64_t    last_frame = 0;
	uint64_t    tests = 0;
	uint64_t    failures = 0;
	uint64_t    streak = 0;
	uint64_t    max_streak = 0;
	uint64_t    last_value = 0;
	bool        last_met = false;
	uint64_t    sequence = 0;
	uint64_t    address = 0;
	uint64_t    reference = 0;
	uint64_t    mask = 0;
	const char* packet = nullptr;
	uint32_t    queue = 0;
	uint32_t    function = 0;
};

struct State {
	std::mutex mutex;
	std::unordered_map<uint64_t, WriteRecord> writes;
	std::map<std::string, WaitRecord> waits;
	std::vector<uint64_t> watch;
	Clock::time_point start = Clock::now();
	Clock::time_point last_report = start;
	uint64_t sequence = 0;
	bool enabled = false;

	State() {
		const char* value = std::getenv("KYTY_DIAG_GPU_LABELS");
		enabled = value != nullptr && value[0] == '1';
		if (!enabled) {
			return;
		}
		const char* addresses = std::getenv("KYTY_DIAG_GPU_LABELS_WATCH");
		while (addresses != nullptr && addresses[0] != '\0') {
			char* end = nullptr;
			const uint64_t address = std::strtoull(addresses, &end, 0);
			if (end != addresses) {
				watch.push_back(address);
			}
			if (end == nullptr || end[0] != ',') {
				break;
			}
			addresses = end + 1;
		}
	}
};

State& GetState() {
	static State state;
	return state;
}

uint32_t Frame() {
	return DebugPresentCount().load(std::memory_order_relaxed);
}

const char* QueueName(uint32_t queue) {
	return queue >= 0x20 ? "ACB" : "DCB";
}

uint64_t ReadValue(const volatile void* data, uint64_t size) {
	uint64_t value = 0;
	if (data == nullptr) {
		return value;
	}
	const auto* bytes = static_cast<const volatile uint8_t*>(data);
	for (uint64_t i = 0; i < size && i < sizeof(value); i++) {
		value |= static_cast<uint64_t>(bytes[i]) << (i * 8u);
	}
	return value;
}

void ReportLocked(State& state, Clock::time_point now) {
	if (now - state.last_report < std::chrono::seconds(20)) {
		return;
	}
	state.last_report = now;
	std::fprintf(stderr, "GPU_LABEL_DUMP elapsed_s=%lld addresses=%zu waits=%zu\n",
	             static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(now - state.start).count()),
	             state.writes.size(), state.waits.size());

	std::vector<const WriteRecord*> writes;
	writes.reserve(state.writes.size());
	for (const auto& [address, record]: state.writes) {
		writes.push_back(&record);
	}
	std::ranges::sort(writes, [](const auto* a, const auto* b) { return a->sequence > b->sequence; });
	for (size_t i = 0; i < writes.size() && i < 64u; i++) {
		const auto& record = *writes[i];
		std::fprintf(stderr,
		             "GPU_LABEL addr=0x%llx first_frame=%llu last_frame=%llu count=%llu value=0x%llx size=%llu "
		             "packet=%s queue=%s queue_id=0x%x\n",
		             static_cast<unsigned long long>(record.address), static_cast<unsigned long long>(record.first_frame),
		             static_cast<unsigned long long>(record.last_frame), static_cast<unsigned long long>(record.count),
		             static_cast<unsigned long long>(record.last_value), static_cast<unsigned long long>(record.size),
		             record.packet, QueueName(record.queue), record.queue);
	}

	std::vector<const WaitRecord*> waits;
	for (const auto& [key, record]: state.waits) {
		if (record.failures != 0) {
			waits.push_back(&record);
		}
	}
	std::ranges::sort(waits, [](const auto* a, const auto* b) {
		if (a->streak != b->streak) {
			return a->streak > b->streak;
		}
		if (a->failures != b->failures) {
			return a->failures > b->failures;
		}
		return a->sequence > b->sequence;
	});
	for (size_t i = 0; i < waits.size() && i < 64u; i++) {
		const auto& record = *waits[i];
		uint64_t write_count = 0;
		uint64_t last_write = 0;
		uint64_t write_sequence = 0;
		for (const auto& [address, write]: state.writes) {
			if (record.address < address || record.address - address >= write.size || write.sequence < write_sequence) {
				continue;
			}
			write_count = write.count;
			last_write = write.last_value;
			write_sequence = write.sequence;
		}
		std::fprintf(stderr,
		             "GPU_WAIT_STUCK kind=%s queue=%s queue_id=0x%x addr=0x%llx ref=0x%llx mask=0x%llx func=%u "
		             "value=0x%llx met=%u tests=%llu failures=%llu streak=%llu max_streak=%llu writes=%llu "
		             "last_write=0x%llx first_frame=%llu last_frame=%llu\n",
		             record.packet, QueueName(record.queue), record.queue,
		             static_cast<unsigned long long>(record.address), static_cast<unsigned long long>(record.reference),
		             static_cast<unsigned long long>(record.mask), record.function,
		             static_cast<unsigned long long>(record.last_value), record.last_met ? 1u : 0u,
		             static_cast<unsigned long long>(record.tests),
		             static_cast<unsigned long long>(record.failures), static_cast<unsigned long long>(record.streak),
		             static_cast<unsigned long long>(record.max_streak), static_cast<unsigned long long>(write_count),
		             static_cast<unsigned long long>(last_write), static_cast<unsigned long long>(record.first_frame),
		             static_cast<unsigned long long>(record.last_frame));
	}
	std::fflush(stderr);
}

void RecordRange(const char* packet, uint32_t queue, uint64_t address, const volatile void* value, uint64_t size,
                 uint64_t last_value) {
	if (!Enabled() || address == 0 || size == 0) {
		return;
	}
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	const uint32_t frame = Frame();
	auto& record = state.writes[address];
	if (record.count == 0) {
		record.address = address;
		record.first_frame = frame;
	}
	record.last_frame = frame;
	record.count++;
	record.last_value = last_value;
	record.size = size;
	record.sequence = ++state.sequence;
	record.packet = packet;
	record.queue = queue;

	const auto* bytes = value != nullptr ? static_cast<const volatile uint8_t*>(value)
	                                     : reinterpret_cast<const volatile uint8_t*>(&last_value);
	for (const uint64_t watched: state.watch) {
		if (watched < address || watched - address >= size) {
			continue;
		}
		const uint64_t watched_value = ReadValue(bytes + (watched - address), size - (watched - address));
		std::fprintf(stderr,
		             "GPU_WRITE_WATCH addr=0x%llx value=0x%llx size=%llu packet=%s queue=%s queue_id=0x%x "
		             "frame=%u count=%llu range=0x%llx\n",
		             static_cast<unsigned long long>(watched), static_cast<unsigned long long>(watched_value),
		             static_cast<unsigned long long>(size), packet, QueueName(queue), queue, frame,
		             static_cast<unsigned long long>(record.count), static_cast<unsigned long long>(address));
	}
	ReportLocked(state, Clock::now());
}

} // namespace

bool Enabled() {
	return GetState().enabled;
}

void Write(const char* packet, uint32_t queue, uint64_t address, uint64_t value, uint32_t size) {
	Common::GuestWaits::RecordGpuWrite(packet, queue, address, value, size);
	RecordRange(packet, queue, address, nullptr, size, value);
}

void WriteRange(const char* packet, uint32_t queue, uint64_t address, const volatile void* value, uint64_t size) {
	if (value == nullptr || size == 0 || (!Enabled() && !Common::GuestWaits::GuestThreadsEnabled())) {
		return;
	}
	const auto* bytes = static_cast<const volatile uint8_t*>(value);
	const uint64_t value_size = std::min<uint64_t>(size, sizeof(uint64_t));
	const uint64_t last_value = ReadValue(bytes + size - value_size, value_size);
	Common::GuestWaits::RecordGpuWrite(packet, queue, address, last_value, size);
	if (!Enabled()) {
		return;
	}
	RecordRange(packet, queue, address, value, size, last_value);
}

void Wait(const char* packet, uint32_t queue, uint32_t function, uint64_t address, uint64_t reference,
          uint64_t mask, uint64_t value, bool met) {
	if (!Enabled()) {
		return;
	}
	auto& state = GetState();
	std::scoped_lock lock(state.mutex);
	const uint32_t frame = Frame();
	char key_text[192] {};
	std::snprintf(key_text, sizeof(key_text), "%s/%x/%llx/%llx/%llx/%u", packet, queue,
	              static_cast<unsigned long long>(address), static_cast<unsigned long long>(reference),
	              static_cast<unsigned long long>(mask), function);
	auto& record = state.waits[key_text];
	if (record.tests == 0) {
		record.first_frame = frame;
		record.packet = packet;
		record.queue = queue;
		record.function = function;
		record.address = address;
		record.reference = reference;
		record.mask = mask;
	}
	record.last_frame = frame;
	record.tests++;
	record.failures += met ? 0 : 1;
	record.streak = met ? 0 : record.streak + 1;
	record.max_streak = std::max(record.max_streak, record.streak);
	record.last_value = value;
	record.last_met = met;
	record.sequence = ++state.sequence;
	std::fprintf(stderr,
	             "GPU_WAIT kind=%s queue=%s queue_id=0x%x addr=0x%llx ref=0x%llx mask=0x%llx func=%u "
	             "value=0x%llx met=%u tests=%llu failures=%llu streak=%llu frame=%u\n",
	             packet, QueueName(queue), queue, static_cast<unsigned long long>(address),
	             static_cast<unsigned long long>(reference), static_cast<unsigned long long>(mask), function,
	             static_cast<unsigned long long>(value), met ? 1u : 0u, static_cast<unsigned long long>(record.tests),
	             static_cast<unsigned long long>(record.failures), static_cast<unsigned long long>(record.streak), frame);
	ReportLocked(state, Clock::now());
}

} // namespace Libs::Graphics::GpuLabels
