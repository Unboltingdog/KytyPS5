#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_DESCRIPTORHEAP_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_DESCRIPTORHEAP_H_

#include "common/common.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <deque>
#include <span>
#include <unordered_map>

namespace Libs::Graphics {

struct GraphicContext;
class MasterSemaphore;

class DescriptorHeap {
public:
	DescriptorHeap(GraphicContext& graphics, MasterSemaphore& master_semaphore);
	~DescriptorHeap();
	KYTY_CLASS_NO_COPY(DescriptorHeap);

	[[nodiscard]] vk::DescriptorSet Commit(vk::DescriptorSetLayout layout);

	// Records how many descriptors of each type a set layout holds, so a pool can be sized for a
	// layout larger than the default pool.
	static void NoteLayout(vk::DescriptorSetLayout                         layout,
	                       std::span<const vk::DescriptorSetLayoutBinding> bindings);

private:
	static constexpr uint32_t DescriptorSetBatch = 32;

	struct Batch {
		std::array<vk::DescriptorSet, DescriptorSetBatch> sets {};
		uint32_t                                          size       = 0;
		uint32_t                                          allocation = DescriptorSetBatch;
	};

	[[nodiscard]] bool Allocate(vk::DescriptorSetLayout layout, Batch& batch);
	void               CreateDescriptorPool();
	void               RetirePool();
	[[nodiscard]] bool ReusePool();

	GraphicContext&                                     m_graphics;
	MasterSemaphore&                                    m_master_semaphore;
	vk::DescriptorPool                                  m_current_pool = nullptr;
	struct PendingPool {
		vk::DescriptorPool pool;
		uint64_t           tick;
		uint32_t           generation;
	};
	std::deque<PendingPool>                              m_pending_pools;
	// Descriptors of each type per pool. They grow, never shrink, when a set layout needs more
	// than a pool holds; pools made at an older size are destroyed instead of reused.
	std::array<uint32_t, 4>                              m_pool_sizes {};
	uint32_t                                             m_generation = 0;
	std::unordered_map<vk::DescriptorSetLayout, Batch>  m_sets;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_DESCRIPTORHEAP_H_
