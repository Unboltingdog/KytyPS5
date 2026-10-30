#include "graphics/host_gpu/renderer/pipeline/descriptorHeap.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace Libs::Graphics {
namespace {

constexpr uint32_t   DescriptorHeapCount = 1024;
constexpr std::array DescriptorPoolSizes = {
    vk::DescriptorPoolSize {vk::DescriptorType::eStorageBuffer, 8192},
    vk::DescriptorPoolSize {vk::DescriptorType::eSampledImage, 8192},
    vk::DescriptorPoolSize {vk::DescriptorType::eStorageImage, 1024},
    vk::DescriptorPoolSize {vk::DescriptorType::eSampler, 1024},
};

std::mutex                                                                g_layout_mutex;
std::unordered_map<vk::DescriptorSetLayout, std::array<uint32_t, DescriptorPoolSizes.size()>>
    g_layout_needs;

} // namespace

void DescriptorHeap::NoteLayout(vk::DescriptorSetLayout                         layout,
                                std::span<const vk::DescriptorSetLayoutBinding> bindings) {
	std::array<uint32_t, DescriptorPoolSizes.size()> needs {};
	for (const auto& binding: bindings) {
		for (size_t i = 0; i < DescriptorPoolSizes.size(); i++) {
			if (DescriptorPoolSizes[i].type == binding.descriptorType) {
				needs[i] += binding.descriptorCount;
			}
		}
	}
	std::scoped_lock lock {g_layout_mutex};
	g_layout_needs[layout] = needs;
}

DescriptorHeap::DescriptorHeap(GraphicContext& graphics, MasterSemaphore& master_semaphore)
    : m_graphics(graphics), m_master_semaphore(master_semaphore) {
	CreateDescriptorPool();
}

DescriptorHeap::~DescriptorHeap() {
	m_graphics.device.destroyDescriptorPool(m_current_pool, nullptr);
	for (const auto& [pool, tick]: m_pending_pools) {
		m_master_semaphore.Wait(tick);
		m_graphics.device.destroyDescriptorPool(pool, nullptr);
	}
}

vk::DescriptorSet DescriptorHeap::Commit(vk::DescriptorSetLayout layout) {
	KYTY_PROFILER_FUNCTION();
	EXIT_IF(layout == nullptr);

	auto& batch = m_sets[layout];
	if (batch.size != 0) {
		return batch.sets[--batch.size];
	}
	if (Allocate(layout, batch)) {
		return batch.sets[--batch.size];
	}

	m_pending_pools.emplace_back(m_current_pool, m_master_semaphore.CurrentTick());
	if (const auto& [pool, tick] = m_pending_pools.front(); m_master_semaphore.IsFree(tick)) {
		m_current_pool = pool;
		m_pending_pools.pop_front();
		EXIT_IF(m_graphics.device.resetDescriptorPool(m_current_pool, {}) != vk::Result::eSuccess);
	} else {
		CreateDescriptorPool();
	}

	m_sets.clear();
	auto& fresh_batch = m_sets[layout];
	if (Allocate(layout, fresh_batch)) {
		return fresh_batch.sets[--fresh_batch.size];
	}
	// PPSA26344: one set layout can need more descriptors of a type than a default pool holds.
	// Give it a pool sized for a batch of its sets.
	std::array<uint32_t, DescriptorPoolSizes.size()> needs {};
	{
		std::scoped_lock lock {g_layout_mutex};
		if (const auto it = g_layout_needs.find(layout); it != g_layout_needs.end()) {
			needs = it->second;
		}
	}
	std::array<uint32_t, DescriptorPoolSizes.size()> minimum {};
	for (size_t i = 0; i < needs.size(); i++) {
		minimum[i] = static_cast<uint32_t>(
		    std::min<uint64_t>(uint64_t {needs[i]} * DescriptorSetBatch, UINT32_MAX));
	}
	LOGF("DescriptorHeap: a set needs storage buffers=%u sampled images=%u storage images=%u "
	     "samplers=%u; sizing a pool for it\n",
	     needs[0], needs[1], needs[2], needs[3]);
	m_pending_pools.emplace_back(m_current_pool, m_master_semaphore.CurrentTick());
	CreateDescriptorPool(minimum);
	fresh_batch = {};
	if (!Allocate(layout, fresh_batch)) {
		EXIT("DescriptorHeap: a set needing storage buffers=%u sampled images=%u storage "
		     "images=%u samplers=%u does not fit a pool sized for it\n",
		     needs[0], needs[1], needs[2], needs[3]);
	}
	return fresh_batch.sets[--fresh_batch.size];
}

bool DescriptorHeap::Allocate(vk::DescriptorSetLayout layout, Batch& batch) {
	std::array<vk::DescriptorSetLayout, DescriptorSetBatch> layouts;
	layouts.fill(layout);

	vk::DescriptorSetAllocateInfo allocate {};
	allocate.descriptorPool = m_current_pool;
	allocate.pSetLayouts    = layouts.data();

	for (;;) {
		allocate.descriptorSetCount = batch.allocation;
		const auto result = m_graphics.device.allocateDescriptorSets(&allocate, batch.sets.data());
		if (result == vk::Result::eSuccess) {
			batch.size = batch.allocation;
			return true;
		}
		EXIT_IF(result != vk::Result::eErrorOutOfPoolMemory &&
		        result != vk::Result::eErrorFragmentedPool);
		if (batch.allocation == 1) {
			return false;
		}
		batch.allocation /= 2;
	}
}

void DescriptorHeap::CreateDescriptorPool(std::span<const uint32_t> minimum) {
	auto sizes = DescriptorPoolSizes;
	for (size_t i = 0; i < sizes.size() && i < minimum.size(); i++) {
		sizes[i].descriptorCount = std::max(sizes[i].descriptorCount, minimum[i]);
	}
	vk::DescriptorPoolCreateInfo create {};
	create.maxSets       = DescriptorHeapCount;
	create.poolSizeCount = static_cast<uint32_t>(sizes.size());
	create.pPoolSizes    = sizes.data();
	EXIT_IF(m_graphics.device.createDescriptorPool(&create, nullptr, &m_current_pool) !=
	        vk::Result::eSuccess);
}

} // namespace Libs::Graphics
