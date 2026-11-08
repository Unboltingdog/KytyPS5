#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <spirv/unified1/spirv.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

uint64_t Milliseconds(Clock::time_point begin) {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count());
}

const char* ResultName(VkResult result) {
	switch (result) {
		case VK_SUCCESS: return "VK_SUCCESS";
		case VK_NOT_READY: return "VK_NOT_READY";
		case VK_TIMEOUT: return "VK_TIMEOUT";
		case VK_EVENT_SET: return "VK_EVENT_SET";
		case VK_EVENT_RESET: return "VK_EVENT_RESET";
		case VK_INCOMPLETE: return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
		case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
		case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
		case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
		case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
		case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
		case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
		case VK_ERROR_NOT_PERMITTED: return "VK_ERROR_NOT_PERMITTED";
		case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
		default: return "VK_RESULT";
	}
}

bool ParseUnsigned(const char* text, uint64_t& value) {
	if (text == nullptr || *text == '\0' || *text == '-') return false;
	char* end = nullptr;
	value     = std::strtoull(text, &end, 10);
	return end != text && end != nullptr && *end == '\0';
}

std::string ExecutablePath(const char* argv0) {
#ifdef _WIN32
	std::array<char, 32768> path {};
	const auto              size = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (size != 0 && size < path.size()) return std::string(path.data(), size);
#else
	std::array<char, 4096> path {};
	const auto             size = readlink("/proc/self/exe", path.data(), path.size() - 1);
	if (size > 0) return std::string(path.data(), static_cast<size_t>(size));
#endif
	std::error_code ec;
	auto            absolute = std::filesystem::absolute(argv0, ec);
	return ec ? std::string(argv0) : absolute.string();
}

struct VulkanApi {
#ifdef _WIN32
	HMODULE library = nullptr;
#else
	void* library = nullptr;
#endif
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
	PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion = nullptr;
	PFN_vkCreateInstance CreateInstance = nullptr;
	PFN_vkDestroyInstance DestroyInstance = nullptr;
	PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
	PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
	PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2 = nullptr;
	PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
	PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
	PFN_vkCreateDevice CreateDevice = nullptr;
	PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
	PFN_vkDestroyDevice DestroyDevice = nullptr;
	PFN_vkCreateShaderModule CreateShaderModule = nullptr;
	PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
	PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
	PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
	PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
	PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
	PFN_vkCreatePipelineCache CreatePipelineCache = nullptr;
	PFN_vkDestroyPipelineCache DestroyPipelineCache = nullptr;
	PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
	PFN_vkDestroyPipeline DestroyPipeline = nullptr;

	~VulkanApi() {
#ifdef _WIN32
		if (library != nullptr) FreeLibrary(library);
#else
		if (library != nullptr) dlclose(library);
#endif
	}

	bool Open() {
#ifdef _WIN32
		library = LoadLibraryA("vulkan-1.dll");
		if (library == nullptr) return false;
		GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr"));
#else
		library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr) library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr) return false;
		GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#endif
		if (GetInstanceProcAddr == nullptr) return false;
		EnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
		    GetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
		CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(GetInstanceProcAddr(nullptr, "vkCreateInstance"));
		return CreateInstance != nullptr;
	}

	bool LoadInstance(VkInstance instance) {
#define SPVBENCH_INSTANCE_FN(name)                                                                                       \
	name = reinterpret_cast<PFN_vk##name>(GetInstanceProcAddr(instance, "vk" #name));                                   \
	if (name == nullptr) return false
		SPVBENCH_INSTANCE_FN(DestroyInstance);
		SPVBENCH_INSTANCE_FN(EnumeratePhysicalDevices);
		SPVBENCH_INSTANCE_FN(GetPhysicalDeviceProperties);
		SPVBENCH_INSTANCE_FN(GetPhysicalDeviceProperties2);
		SPVBENCH_INSTANCE_FN(GetPhysicalDeviceFeatures2);
		SPVBENCH_INSTANCE_FN(EnumerateDeviceExtensionProperties);
		SPVBENCH_INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties);
		SPVBENCH_INSTANCE_FN(CreateDevice);
		SPVBENCH_INSTANCE_FN(GetDeviceProcAddr);
#undef SPVBENCH_INSTANCE_FN
		return true;
	}

	bool LoadDevice(VkDevice device) {
#define SPVBENCH_DEVICE_FN(name)                                                                                         \
	name = reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device, "vk" #name));                                        \
	if (name == nullptr) return false
		SPVBENCH_DEVICE_FN(DestroyDevice);
		SPVBENCH_DEVICE_FN(CreateShaderModule);
		SPVBENCH_DEVICE_FN(DestroyShaderModule);
		SPVBENCH_DEVICE_FN(CreateDescriptorSetLayout);
		SPVBENCH_DEVICE_FN(DestroyDescriptorSetLayout);
		SPVBENCH_DEVICE_FN(CreatePipelineLayout);
		SPVBENCH_DEVICE_FN(DestroyPipelineLayout);
		SPVBENCH_DEVICE_FN(CreatePipelineCache);
		SPVBENCH_DEVICE_FN(DestroyPipelineCache);
		SPVBENCH_DEVICE_FN(CreateComputePipelines);
		SPVBENCH_DEVICE_FN(DestroyPipeline);
#undef SPVBENCH_DEVICE_FN
		return true;
	}
};

struct FeatureChain {
	VkPhysicalDeviceFeatures base {};
	VkPhysicalDeviceVulkan11Features features11 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
	VkPhysicalDeviceVulkan12Features features12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	VkPhysicalDeviceVulkan13Features features13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	VkPhysicalDeviceRobustness2FeaturesEXT robustness2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
	VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR workgroup {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR};
	VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR};
	VkPhysicalDeviceMaintenance6FeaturesKHR maintenance6 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR};
	VkPhysicalDeviceMaintenance7FeaturesKHR maintenance7 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR};
	VkPhysicalDeviceMaintenance8FeaturesKHR maintenance8 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR};
	VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR integer_dot {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR};
	VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT image_atomic {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT};
	VkPhysicalDeviceShaderClockFeaturesKHR clock {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
	VkPhysicalDeviceMeshShaderFeaturesEXT mesh {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
	VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR};
	VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT feedback_layout {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT};
	VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT feedback_dynamic {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT};
	VkPhysicalDeviceProvokingVertexFeaturesEXT provoking {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT};
	VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT};
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
	VkPhysicalDeviceColorWriteEnableFeaturesEXT color_write {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT};
	VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
	VkPhysicalDeviceDepthClipControlFeaturesEXT depth_control {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT};
	VkPhysicalDeviceImageViewMinLodFeaturesEXT min_lod {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT};
};

class VulkanSession {
public:
	~VulkanSession() { Reset(); }

	void Reset() {
		if (device != VK_NULL_HANDLE && api.DestroyDevice != nullptr) api.DestroyDevice(device, nullptr);
		if (instance != VK_NULL_HANDLE && api.DestroyInstance != nullptr) api.DestroyInstance(instance, nullptr);
		device = VK_NULL_HANDLE;
		instance = VK_NULL_HANDLE;
	}

	VkResult Init(int requested_index) {
		if (!api.Open()) return VK_ERROR_INITIALIZATION_FAILED;
		uint32_t loader_version = VK_API_VERSION_1_0;
		if (api.EnumerateInstanceVersion != nullptr) {
			const VkResult result = api.EnumerateInstanceVersion(&loader_version);
			if (result != VK_SUCCESS) return result;
		}
		if (loader_version < VK_API_VERSION_1_3) return VK_ERROR_INCOMPATIBLE_DRIVER;
		VkApplicationInfo application {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		application.pApplicationName   = "kyty_spvbench";
		application.applicationVersion = 1;
		application.pEngineName        = "Kyty";
		application.apiVersion         = VK_API_VERSION_1_3;
		VkInstanceCreateInfo create {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		create.pApplicationInfo = &application;
		VkResult result = api.CreateInstance(&create, nullptr, &instance);
		if (result != VK_SUCCESS) return result;
		if (!api.LoadInstance(instance)) return VK_ERROR_INITIALIZATION_FAILED;

		uint32_t device_count = 0;
		result = api.EnumeratePhysicalDevices(instance, &device_count, nullptr);
		if (result != VK_SUCCESS) return result;
		if (device_count == 0) return VK_ERROR_INITIALIZATION_FAILED;
		std::vector<VkPhysicalDevice> devices(device_count);
		result = api.EnumeratePhysicalDevices(instance, &device_count, devices.data());
		if (result != VK_SUCCESS) return result;
		if (requested_index >= 0) {
			if (static_cast<size_t>(requested_index) >= devices.size()) return VK_ERROR_INITIALIZATION_FAILED;
			device_index = static_cast<uint32_t>(requested_index);
		} else {
			device_index = 0;
			for (uint32_t i = 0; i < devices.size(); i++) {
				VkPhysicalDeviceProperties properties {};
				api.GetPhysicalDeviceProperties(devices[i], &properties);
				if (properties.vendorID == 0x1002u) {
					device_index = i;
					break;
				}
			}
		}
		physical = devices[device_index];
		api.GetPhysicalDeviceProperties(physical, &properties);
		result = SelectQueue();
		if (result != VK_SUCCESS) return result;
		result = SelectExtensions();
		if (result != VK_SUCCESS) return result;
		return CreateDevice();
	}

	std::string Description() const {
		char text[1024];
		std::snprintf(text, sizeof(text),
		              "SPVBENCH_DEVICE index=%u name=%s vendor=0x%04x device=0x%04x driver=0x%08x api=%u.%u.%u",
		              device_index, properties.deviceName, properties.vendorID, properties.deviceID,
		              properties.driverVersion, VK_API_VERSION_MAJOR(properties.apiVersion),
		              VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
		return text;
	}

	VkResult Compile(const std::vector<uint32_t>& words, const class Reflection& reflection, bool no_cache,
	                 VkPipeline* out_pipeline = nullptr);

	const VkPhysicalDeviceDescriptorIndexingProperties& DescriptorLimits() const { return descriptor_properties; }
	uint64_t LastCompileMs() const { return last_compile_ms; }

private:
	VkResult SelectQueue() {
		uint32_t count = 0;
		api.GetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		api.GetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
		for (uint32_t i = 0; i < count; i++) {
			if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
			    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
				queue_family = i;
				return VK_SUCCESS;
			}
		}
		for (uint32_t i = 0; i < count; i++) {
			if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
				queue_family = i;
				return VK_SUCCESS;
			}
		}
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	bool HasExtension(const char* name) const {
		return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
			return std::strcmp(extension.extensionName, name) == 0;
		});
	}

	VkResult SelectExtensions() {
		uint32_t count = 0;
		VkResult result = api.EnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
		if (result != VK_SUCCESS) return result;
		extensions.resize(count);
		result = api.EnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
		if (result != VK_SUCCESS) return result;
		static constexpr const char* wanted[] = {
		    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		    VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME,
		    VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME,
		    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		    VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
		    VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
		    VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
		    VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
		    VK_KHR_MAINTENANCE_6_EXTENSION_NAME,
		    VK_KHR_MAINTENANCE_7_EXTENSION_NAME,
		    VK_KHR_MAINTENANCE_8_EXTENSION_NAME,
		    VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
		    VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME,
		    VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
		    VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
		    VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME,
		    VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME,
		    VK_EXT_PROVOKING_VERTEX_EXTENSION_NAME,
		    VK_EXT_MESH_SHADER_EXTENSION_NAME,
		    VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME,
		    VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME,
		    VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
		    VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME,
		    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
		    VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME,
		    VK_EXT_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_EXTENSION_NAME,
		};
		for (const char* extension: wanted) {
			if (HasExtension(extension)) enabled_extensions.push_back(extension);
		}
		return VK_SUCCESS;
	}

	VkResult CreateDevice() {
		const void* head = nullptr;
		const auto append = [&head](auto& feature) {
			feature.pNext = const_cast<void*>(head);
			head          = &feature;
		};
		append(features.features11);
		append(features.features12);
		append(features.features13);
		if (HasExtension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) append(features.robustness2);
		if (HasExtension(VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME)) append(features.workgroup);
		if (HasExtension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME)) append(features.maintenance5);
		if (HasExtension(VK_KHR_MAINTENANCE_6_EXTENSION_NAME)) append(features.maintenance6);
		if (HasExtension(VK_KHR_MAINTENANCE_7_EXTENSION_NAME)) append(features.maintenance7);
		if (HasExtension(VK_KHR_MAINTENANCE_8_EXTENSION_NAME)) append(features.maintenance8);
		if (HasExtension(VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME)) append(features.integer_dot);
		if (HasExtension(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME)) append(features.image_atomic);
		if (HasExtension(VK_KHR_SHADER_CLOCK_EXTENSION_NAME)) append(features.clock);
		if (HasExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME)) append(features.mesh);
		if (HasExtension(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME)) append(features.barycentric);
		if (HasExtension(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME)) append(features.feedback_layout);
		if (HasExtension(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_EXTENSION_NAME)) append(features.feedback_dynamic);
		if (HasExtension(VK_EXT_PROVOKING_VERTEX_EXTENSION_NAME)) append(features.provoking);
		if (HasExtension(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME)) append(features.conditional);
		if (HasExtension(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)) append(features.dynamic_state);
		if (HasExtension(VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME)) append(features.color_write);
		if (HasExtension(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) append(features.depth_clip);
		if (HasExtension(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME)) append(features.depth_control);
		if (HasExtension(VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME)) append(features.min_lod);

		VkPhysicalDeviceFeatures2 supported {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		supported.pNext = const_cast<void*>(head);
		api.GetPhysicalDeviceFeatures2(physical, &supported);
		features.base = supported.features;

		VkPhysicalDeviceVulkan11Properties properties11 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
		VkPhysicalDeviceVulkan12Properties properties12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
		VkPhysicalDeviceSubgroupSizeControlProperties subgroup {
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
		descriptor_properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
		properties12.pNext = &subgroup;
		properties11.pNext = &properties12;
		descriptor_properties.pNext = &properties11;
		VkPhysicalDeviceProperties2 query {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
		query.pNext = &descriptor_properties;
		api.GetPhysicalDeviceProperties2(physical, &query);
		subgroup_properties = subgroup;
		default_subgroup_size = properties11.subgroupSize;

		float priority = 1.0f;
		VkDeviceQueueCreateInfo queue {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		queue.queueFamilyIndex = queue_family;
		queue.queueCount       = 1;
		queue.pQueuePriorities = &priority;
		VkDeviceCreateInfo create {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		create.pNext                   = &supported;
		create.queueCreateInfoCount    = 1;
		create.pQueueCreateInfos       = &queue;
		create.enabledExtensionCount   = static_cast<uint32_t>(enabled_extensions.size());
		create.ppEnabledExtensionNames = enabled_extensions.data();
		const VkResult result = api.CreateDevice(physical, &create, nullptr, &device);
		if (result != VK_SUCCESS) return result;
		if (!api.LoadDevice(device)) return VK_ERROR_INITIALIZATION_FAILED;
		return VK_SUCCESS;
	}

	VulkanApi api;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t device_index = 0;
	uint32_t queue_family = 0;
	uint32_t default_subgroup_size = 0;
	uint64_t last_compile_ms = 0;
	VkPhysicalDeviceProperties properties {};
	VkPhysicalDeviceSubgroupSizeControlProperties subgroup_properties {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
	VkPhysicalDeviceDescriptorIndexingProperties descriptor_properties {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
	std::vector<VkExtensionProperties> extensions;
	std::vector<const char*> enabled_extensions;
	FeatureChain features {};

	friend class ReflectionCompiler;
};

struct TypeInfo {
	spv::Op op = spv::OpNop;
	std::vector<uint32_t> operands;
};

struct VariableInfo {
	uint32_t pointer_type = 0;
	uint32_t id = 0;
	spv::StorageClass storage = spv::StorageClassFunction;
};

struct DecorationInfo {
	spv::Decoration decoration = spv::DecorationRelaxedPrecision;
	std::vector<uint32_t> operands;
};

class Reflection {
public:
	bool Parse(const std::vector<uint32_t>& words, std::string& error) {
		if (words.size() < 5 || words[0] != spv::MagicNumber) {
			error = "not a SPIR-V module";
			return false;
		}
		for (size_t offset = 5; offset < words.size();) {
			const uint32_t first = words[offset];
			const uint16_t count = static_cast<uint16_t>(first >> 16u);
			const spv::Op op = static_cast<spv::Op>(first & 0xffffu);
			if (count == 0 || offset + count > words.size()) {
				error = "truncated SPIR-V instruction";
				return false;
			}
			const uint32_t* instruction = words.data() + offset;
			switch (op) {
				case spv::OpEntryPoint:
					if (count >= 4 && instruction[1] == spv::ExecutionModelGLCompute && entry_point.empty()) {
						entry_point = LiteralString(instruction + 3, count - 3);
					}
					break;
				case spv::OpTypeVoid:
				case spv::OpTypeBool:
				case spv::OpTypeInt:
				case spv::OpTypeFloat:
				case spv::OpTypeVector:
				case spv::OpTypeMatrix:
				case spv::OpTypeImage:
				case spv::OpTypeSampler:
				case spv::OpTypeSampledImage:
				case spv::OpTypeArray:
				case spv::OpTypeRuntimeArray:
				case spv::OpTypeStruct:
				case spv::OpTypePointer:
					if (count >= 2) types[instruction[1]] = {op, {instruction + 2, instruction + count}};
					break;
				case spv::OpVariable:
					if (count >= 4) variables.push_back({instruction[1], instruction[2], static_cast<spv::StorageClass>(instruction[3])});
					break;
				case spv::OpDecorate:
					if (count >= 3) decorations[instruction[1]].push_back(
					    {static_cast<spv::Decoration>(instruction[2]), {instruction + 3, instruction + count}});
					break;
				case spv::OpMemberDecorate:
					if (count >= 4) member_decorations[{instruction[1], instruction[2]}].push_back(
					    {static_cast<spv::Decoration>(instruction[3]), {instruction + 4, instruction + count}});
					break;
				case spv::OpConstant:
				case spv::OpSpecConstant:
					if (count >= 4) constants[instruction[2]] = ConstantBits(instruction + 3, count - 3);
					break;
				case spv::OpConstantTrue:
				case spv::OpSpecConstantTrue:
					if (count >= 3) constants[instruction[2]] = 1;
					break;
				case spv::OpConstantFalse:
				case spv::OpSpecConstantFalse:
					if (count >= 3) constants[instruction[2]] = 0;
					break;
				default: break;
			}
			offset += count;
		}
		if (entry_point.empty()) {
			error = "no GLCompute entry point";
			return false;
		}
		return BuildBindings(error);
	}

	const std::string& EntryPoint() const { return entry_point; }

	uint32_t PushBytes() const { return push_bytes; }

private:
	static std::string LiteralString(const uint32_t* words, uint32_t count) {
		std::string out;
		for (uint32_t i = 0; i < count; i++) {
			for (uint32_t byte = 0; byte < 4; byte++) {
				const char c = static_cast<char>((words[i] >> (byte * 8u)) & 0xffu);
				if (c == '\0') return out;
				out.push_back(c);
			}
		}
		return out;
	}

	static uint64_t ConstantBits(const uint32_t* words, uint32_t count) {
		uint64_t value = words[0];
		if (count > 1) value |= static_cast<uint64_t>(words[1]) << 32u;
		return value;
	}

	bool HasDecoration(uint32_t id, spv::Decoration wanted) const {
		const auto found = decorations.find(id);
		return found != decorations.end() && std::ranges::any_of(found->second, [wanted](const DecorationInfo& item) {
			return item.decoration == wanted;
		});
	}

	std::optional<uint32_t> DecorationValue(uint32_t id, spv::Decoration wanted) const {
		const auto found = decorations.find(id);
		if (found == decorations.end()) return std::nullopt;
		for (const auto& item: found->second) {
			if (item.decoration == wanted && !item.operands.empty()) return item.operands[0];
		}
		return std::nullopt;
	}

	std::optional<uint32_t> MemberOffset(uint32_t id, uint32_t member) const {
		const auto found = member_decorations.find({id, member});
		if (found == member_decorations.end()) return std::nullopt;
		for (const auto& item: found->second) {
			if (item.decoration == spv::DecorationOffset && !item.operands.empty()) return item.operands[0];
		}
		return std::nullopt;
	}

	bool DescriptorFor(const VariableInfo& variable, uint32_t set, uint32_t binding, std::string& error) {
		const auto pointer = types.find(variable.pointer_type);
		if (pointer == types.end() || pointer->second.op != spv::OpTypePointer || pointer->second.operands.size() < 2) {
			error = "bad descriptor pointer";
			return false;
		}
		uint32_t type_id = pointer->second.operands[1];
		uint64_t count = 1;
		bool runtime = false;
		while (true) {
			const auto type = types.find(type_id);
			if (type == types.end()) {
				error = "unknown descriptor type";
				return false;
			}
			if (type->second.op == spv::OpTypeArray && type->second.operands.size() >= 2) {
				const auto length = constants.find(type->second.operands[1]);
				if (length == constants.end() || length->second == 0 || count > UINT32_MAX / length->second) {
					error = "bad descriptor array length";
					return false;
				}
				count *= length->second;
				type_id = type->second.operands[0];
				continue;
			}
			if (type->second.op == spv::OpTypeRuntimeArray) {
				runtime = true;
				type_id = type->second.operands[0];
				continue;
			}
			break;
		}

		VkDescriptorType descriptor_type;
		const auto& base = types[type_id];
		if (variable.storage == spv::StorageClassStorageBuffer) {
			descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		} else if (variable.storage == spv::StorageClassUniform) {
			descriptor_type = HasDecoration(type_id, spv::DecorationBufferBlock) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
			                                                                     : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		} else if (base.op == spv::OpTypeSampler) {
			descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLER;
		} else if (base.op == spv::OpTypeSampledImage) {
			descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		} else if (base.op == spv::OpTypeImage && base.operands.size() >= 7) {
			const spv::Dim dim = static_cast<spv::Dim>(base.operands[1]);
			const uint32_t sampled = base.operands[5];
			if (dim == spv::DimBuffer) {
				descriptor_type = sampled == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
				                               : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			} else {
				descriptor_type = sampled == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			}
		} else {
			error = "unsupported descriptor type";
			return false;
		}
		if (runtime) count = RuntimeCount(descriptor_type);
		if (count == 0 || count > UINT32_MAX) {
			error = "descriptor count exceeds device limits";
			return false;
		}
		auto& list = bindings[set];
		VkDescriptorSetLayoutBinding item {binding, descriptor_type, static_cast<uint32_t>(count),
		                                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
		const auto existing = std::ranges::find_if(list, [binding](const auto& value) { return value.binding == binding; });
		if (existing == list.end()) {
			list.push_back(item);
		} else {
			if (existing->descriptorType != descriptor_type) {
				error = "conflicting descriptor declarations";
				return false;
			}
			existing->descriptorCount = std::max(existing->descriptorCount, item.descriptorCount);
			existing->stageFlags |= item.stageFlags;
		}
		return true;
	}

	uint32_t RuntimeCount(VkDescriptorType type) const {
		uint32_t set_count = 0;
		uint32_t stage_count = 0;
		switch (type) {
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				set_count = limits.maxDescriptorSetUpdateAfterBindSamplers;
				stage_count = limits.maxPerStageDescriptorUpdateAfterBindSamplers;
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				set_count = limits.maxDescriptorSetUpdateAfterBindUniformBuffers;
				stage_count = limits.maxPerStageDescriptorUpdateAfterBindUniformBuffers;
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				set_count = limits.maxDescriptorSetUpdateAfterBindStorageBuffers;
				stage_count = limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers;
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				set_count = limits.maxDescriptorSetUpdateAfterBindStorageImages;
				stage_count = limits.maxPerStageDescriptorUpdateAfterBindStorageImages;
				break;
			default:
				set_count = limits.maxDescriptorSetUpdateAfterBindSampledImages;
				stage_count = limits.maxPerStageDescriptorUpdateAfterBindSampledImages;
				break;
		}
		const uint32_t count = std::min(set_count, stage_count);
		return count == 0 ? 1 : count;
	}

	bool TypeSize(uint32_t type_id, uint32_t depth, uint64_t& size, std::string& error) const {
		if (depth > 64) {
			error = "recursive push-constant type";
			return false;
		}
		const auto found = types.find(type_id);
		if (found == types.end()) {
			error = "unknown push-constant type";
			return false;
		}
		const auto& type = found->second;
		switch (type.op) {
			case spv::OpTypeBool:
			case spv::OpTypeInt:
			case spv::OpTypeFloat:
				if (type.operands.empty() || type.operands[0] % 8u != 0) return false;
				size = type.operands[0] / 8u;
				return true;
			case spv::OpTypeVector:
			case spv::OpTypeMatrix: {
				uint64_t element = 0;
				if (type.operands.size() < 2 || !TypeSize(type.operands[0], depth + 1, element, error)) return false;
				size = element * type.operands[1];
				return true;
			}
			case spv::OpTypeArray: {
				if (type.operands.size() < 2) return false;
				const auto length = constants.find(type.operands[1]);
				uint64_t element = 0;
				if (length == constants.end() || !TypeSize(type.operands[0], depth + 1, element, error)) return false;
				const auto stride = DecorationValue(type_id, spv::DecorationArrayStride);
				size = (stride.has_value() ? *stride : element) * length->second;
				return true;
			}
			case spv::OpTypeStruct: {
				uint64_t end = 0;
				uint64_t sequential = 0;
				for (uint32_t member = 0; member < type.operands.size(); member++) {
					uint64_t member_size = 0;
					if (!TypeSize(type.operands[member], depth + 1, member_size, error)) return false;
					const auto offset = MemberOffset(type_id, member);
					const uint64_t begin = offset.has_value() ? *offset : sequential;
					end = std::max(end, begin + member_size);
					sequential = begin + member_size;
				}
				size = end;
				return true;
			}
			default: break;
		}
		error = "unsupported push-constant type";
		return false;
	}

	bool BuildBindings(std::string& error) {
		for (const auto& variable: variables) {
			if (variable.storage != spv::StorageClassPushConstant) continue;
			const auto pointer = types.find(variable.pointer_type);
			if (pointer == types.end() || pointer->second.operands.size() < 2) return false;
			uint64_t size = 0;
			if (!TypeSize(pointer->second.operands[1], 0, size, error) || size > UINT32_MAX) return false;
			push_bytes = std::max(push_bytes, static_cast<uint32_t>(size));
		}
		return true;
	}

	std::unordered_map<uint32_t, TypeInfo> types;
	std::unordered_map<uint32_t, std::vector<DecorationInfo>> decorations;
	std::map<std::pair<uint32_t, uint32_t>, std::vector<DecorationInfo>> member_decorations;
	std::unordered_map<uint32_t, uint64_t> constants;
	std::vector<VariableInfo> variables;
	std::string entry_point;
	std::map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> bindings;
	uint32_t push_bytes = 0;
	VkPhysicalDeviceDescriptorIndexingProperties limits {
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
	friend class VulkanSession;
};

VkResult VulkanSession::Compile(const std::vector<uint32_t>& words, const Reflection& reflection, bool no_cache,
                                VkPipeline* out_pipeline) {
	std::string error;
	Reflection parsed = reflection;
	parsed.limits = descriptor_properties;
	for (const auto& variable: parsed.variables) {
		if (variable.storage != spv::StorageClassUniform && variable.storage != spv::StorageClassUniformConstant &&
		    variable.storage != spv::StorageClassStorageBuffer) {
			continue;
		}
		const auto set = parsed.DecorationValue(variable.id, spv::DecorationDescriptorSet);
		const auto binding = parsed.DecorationValue(variable.id, spv::DecorationBinding);
		if (set.has_value() && binding.has_value() && !parsed.DescriptorFor(variable, *set, *binding, error)) {
			std::fprintf(stderr, "SPVBENCH reflection: %s\n", error.c_str());
			return VK_ERROR_INITIALIZATION_FAILED;
		}
	}
	for (auto& [set, list]: parsed.bindings) {
		(void)set;
		std::ranges::sort(list, [](const auto& a, const auto& b) { return a.binding < b.binding; });
	}

	std::vector<VkDescriptorSetLayout> layouts;
	std::vector<VkDescriptorSetLayout> created_layouts;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipelineCache cache = VK_NULL_HANDLE;
	VkShaderModule module = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = VK_SUCCESS;
	const auto cleanup = [&] {
		if (pipeline != VK_NULL_HANDLE) api.DestroyPipeline(device, pipeline, nullptr);
		if (module != VK_NULL_HANDLE) api.DestroyShaderModule(device, module, nullptr);
		if (cache != VK_NULL_HANDLE) api.DestroyPipelineCache(device, cache, nullptr);
		if (pipeline_layout != VK_NULL_HANDLE) api.DestroyPipelineLayout(device, pipeline_layout, nullptr);
		for (auto layout: created_layouts) api.DestroyDescriptorSetLayout(device, layout, nullptr);
	};

	const uint32_t max_set = parsed.bindings.empty() ? 0 : parsed.bindings.rbegin()->first;
	for (uint32_t set = 0; set <= max_set; set++) {
		const auto found = parsed.bindings.find(set);
		static const std::vector<VkDescriptorSetLayoutBinding> empty;
		const auto& list = found == parsed.bindings.end() ? empty : found->second;
		std::vector<VkDescriptorBindingFlags> flags(list.size(), 0);
		if (found != parsed.bindings.end()) {
			const auto& runtime_variables = parsed.variables;
			for (size_t i = 0; i < list.size(); i++) {
				for (const auto& variable: runtime_variables) {
					const auto variable_set = parsed.DecorationValue(variable.id, spv::DecorationDescriptorSet);
					const auto variable_binding = parsed.DecorationValue(variable.id, spv::DecorationBinding);
					if (!variable_set.has_value() || *variable_set != set || !variable_binding.has_value() ||
					    *variable_binding != list[i].binding) {
						continue;
					}
					const auto pointer = parsed.types.find(variable.pointer_type);
					if (pointer != parsed.types.end() && pointer->second.operands.size() >= 2) {
						uint32_t type_id = pointer->second.operands[1];
						while (parsed.types[type_id].op == spv::OpTypeArray) type_id = parsed.types[type_id].operands[0];
						if (parsed.types[type_id].op == spv::OpTypeRuntimeArray) {
							flags[i] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
							           VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
							           VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
							           VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
						}
					}
				}
			}
		}
		VkDescriptorSetLayoutBindingFlagsCreateInfo flags_create {
		    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
		flags_create.bindingCount = static_cast<uint32_t>(flags.size());
		flags_create.pBindingFlags = flags.data();
		VkDescriptorSetLayoutCreateInfo create {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		create.pNext = &flags_create;
		create.bindingCount = static_cast<uint32_t>(list.size());
		create.pBindings = list.data();
		create.flags = std::ranges::any_of(flags, [](VkDescriptorBindingFlags value) {
			return (value & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) != 0;
		}) ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT : 0;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		result = api.CreateDescriptorSetLayout(device, &create, nullptr, &layout);
		if (result != VK_SUCCESS) {
			cleanup();
			return result;
		}
		created_layouts.push_back(layout);
		layouts.push_back(layout);
		if (max_set == 0 && parsed.bindings.empty()) break;
	}

	VkPushConstantRange push {VK_SHADER_STAGE_COMPUTE_BIT, 0, reflection.PushBytes()};
	VkPipelineLayoutCreateInfo layout_create {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layout_create.setLayoutCount = static_cast<uint32_t>(layouts.size());
	layout_create.pSetLayouts = layouts.data();
	layout_create.pushConstantRangeCount = push.size == 0 ? 0 : 1;
	layout_create.pPushConstantRanges = push.size == 0 ? nullptr : &push;
	result = api.CreatePipelineLayout(device, &layout_create, nullptr, &pipeline_layout);
	if (result != VK_SUCCESS) {
		cleanup();
		return result;
	}
	if (!no_cache) {
		VkPipelineCacheCreateInfo cache_create {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
		result = api.CreatePipelineCache(device, &cache_create, nullptr, &cache);
		if (result != VK_SUCCESS) {
			cleanup();
			return result;
		}
	}
	VkShaderModuleCreateInfo module_create {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	module_create.codeSize = words.size() * sizeof(uint32_t);
	module_create.pCode = words.data();
	result = api.CreateShaderModule(device, &module_create, nullptr, &module);
	if (result != VK_SUCCESS) {
		cleanup();
		return result;
	}
	VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup {
	    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};
	subgroup.requiredSubgroupSize = default_subgroup_size;
	VkPipelineShaderStageCreateInfo stage {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = module;
	stage.pName = reflection.EntryPoint().c_str();
	if (features.features13.subgroupSizeControl && default_subgroup_size >= subgroup_properties.minSubgroupSize &&
	    default_subgroup_size <= subgroup_properties.maxSubgroupSize &&
	    (subgroup_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0) {
		stage.pNext = &subgroup;
	}
	VkComputePipelineCreateInfo create {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	create.stage = stage;
	create.layout = pipeline_layout;
	create.basePipelineIndex = -1;
	const auto begin = Clock::now();
	result = api.CreateComputePipelines(device, cache, 1, &create, nullptr, &pipeline);
	last_compile_ms = Milliseconds(begin);
	if (result == VK_SUCCESS && out_pipeline != nullptr) {
		*out_pipeline = pipeline;
		pipeline = VK_NULL_HANDLE;
	}
	cleanup();
	return result;
}

bool ReadSpirv(const std::filesystem::path& path, std::vector<uint32_t>& words) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) return false;
	const auto size = input.tellg();
	if (size < 20 || size % 4 != 0) return false;
	words.resize(static_cast<size_t>(size) / sizeof(uint32_t));
	input.seekg(0);
	input.read(reinterpret_cast<char*>(words.data()), size);
	return input.good() || input.eof();
}

int RunChild(const std::filesystem::path& path, int device_index, bool no_cache) {
	VkResult result = VK_ERROR_INITIALIZATION_FAILED;
	uint64_t compile_ms = 0;
	std::vector<uint32_t> words;
	if (!ReadSpirv(path, words)) {
		std::fprintf(stderr, "SPVBENCH cannot read %s\n", path.string().c_str());
	} else {
		Reflection reflection;
		std::string error;
		if (!reflection.Parse(words, error)) {
			std::fprintf(stderr, "SPVBENCH reflection of %s: %s\n", path.string().c_str(), error.c_str());
		} else {
			VulkanSession session;
			result = session.Init(device_index);
			if (result == VK_SUCCESS) {
				result = session.Compile(words, reflection, no_cache);
				compile_ms = session.LastCompileMs();
			}
		}
	}
	std::printf("SPVBENCH file=%s bytes=%zu result=%s", path.string().c_str(), words.size() * sizeof(uint32_t),
	            ResultName(result));
	if (ResultName(result) == std::string_view("VK_RESULT")) std::printf("(%d)", static_cast<int>(result));
	std::printf(" ms=%" PRIu64 "\n", compile_ms);
	std::fflush(stdout);
	return 0;
}

struct ChildResult {
	bool timeout = false;
	bool abnormal = false;
	uint64_t code = 0;
	uint64_t ms = 0;
	std::string output;
};

#ifdef _WIN32
std::string QuoteWindows(std::string_view value) {
	std::string out = "\"";
	uint32_t slashes = 0;
	for (const char c: value) {
		if (c == '\\') {
			slashes++;
		} else if (c == '"') {
			out.append(slashes * 2 + 1, '\\');
			out.push_back('"');
			slashes = 0;
		} else {
			out.append(slashes, '\\');
			slashes = 0;
			out.push_back(c);
		}
	}
	out.append(slashes * 2, '\\');
	out.push_back('"');
	return out;
}

ChildResult RunIsolated(const std::string& executable, const std::filesystem::path& path, int device_index,
                        bool no_cache, uint32_t timeout_ms) {
	ChildResult result;
	SECURITY_ATTRIBUTES security {sizeof(security), nullptr, TRUE};
	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
	if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
		result.abnormal = true;
		result.code = GetLastError();
		return result;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
	std::string command = QuoteWindows(executable) + " --child " + QuoteWindows(path.string()) + " --device " +
	                      std::to_string(device_index);
	if (no_cache) command += " --no-cache";
	STARTUPINFOA startup {sizeof(startup)};
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = write_pipe;
	startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION process {};
	std::vector<char> mutable_command(command.begin(), command.end());
	mutable_command.push_back('\0');
	const BOOL created = CreateProcessA(executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE, 0, nullptr,
	                                    nullptr, &startup, &process);
	CloseHandle(write_pipe);
	if (!created) {
		CloseHandle(read_pipe);
		result.abnormal = true;
		result.code = GetLastError();
		return result;
	}
	const auto begin = Clock::now();
	const DWORD waited = WaitForSingleObject(process.hProcess, timeout_ms);
	result.ms = Milliseconds(begin);
	if (waited == WAIT_TIMEOUT) {
		result.timeout = true;
		TerminateProcess(process.hProcess, 0x80000001u);
		WaitForSingleObject(process.hProcess, INFINITE);
	} else if (waited != WAIT_OBJECT_0) {
		result.abnormal = true;
		result.code = GetLastError();
	}
	DWORD exit_code = 0;
	if (!result.timeout && waited == WAIT_OBJECT_0) {
		GetExitCodeProcess(process.hProcess, &exit_code);
		if (exit_code != 0) {
			result.abnormal = true;
			result.code = exit_code;
		}
	}
	std::array<char, 4096> buffer {};
	DWORD read = 0;
	while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read != 0) {
		result.output.append(buffer.data(), read);
	}
	CloseHandle(read_pipe);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return result;
}
#else
ChildResult RunIsolated(const std::string& executable, const std::filesystem::path& path, int device_index,
                        bool no_cache, uint32_t timeout_ms) {
	ChildResult result;
	int pipes[2] = {-1, -1};
	if (pipe(pipes) != 0) {
		result.abnormal = true;
		result.code = static_cast<uint64_t>(errno);
		return result;
	}
	const pid_t child = fork();
	if (child == 0) {
		close(pipes[0]);
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[1]);
		const std::string index = std::to_string(device_index);
		if (no_cache) {
			execl(executable.c_str(), executable.c_str(), "--child", path.c_str(), "--device", index.c_str(),
			      "--no-cache", static_cast<char*>(nullptr));
		} else {
			execl(executable.c_str(), executable.c_str(), "--child", path.c_str(), "--device", index.c_str(),
			      static_cast<char*>(nullptr));
		}
		_Exit(127);
	}
	close(pipes[1]);
	if (child < 0) {
		close(pipes[0]);
		result.abnormal = true;
		result.code = static_cast<uint64_t>(errno);
		return result;
	}
	fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL, 0) | O_NONBLOCK);
	const auto begin = Clock::now();
	int status = 0;
	pid_t waited = 0;
	for (;;) {
		waited = waitpid(child, &status, WNOHANG);
		if (waited != 0) break;
		if (Milliseconds(begin) >= timeout_ms) {
			result.timeout = true;
			kill(child, SIGKILL);
			while (waitpid(child, &status, 0) < 0) {
			}
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	result.ms = Milliseconds(begin);
	if (!result.timeout) {
		if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			result.abnormal = true;
			result.code = waited > 0 && WIFSIGNALED(status) ? static_cast<uint64_t>(WTERMSIG(status))
			                                                : static_cast<uint64_t>(WEXITSTATUS(status));
		}
	}
	std::array<char, 4096> buffer {};
	for (;;) {
		const ssize_t got = read(pipes[0], buffer.data(), buffer.size());
		if (got > 0) {
			result.output.append(buffer.data(), static_cast<size_t>(got));
		} else if (got == 0 || errno != EAGAIN) {
			break;
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	close(pipes[0]);
	return result;
}
#endif

void Trim(std::string& text) {
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
		text.pop_back();
	}
}

void Usage() {
	std::fprintf(stderr,
	             "usage: kyty_spvbench [--timeout SEC=20] [--repeat N=1] [--device INDEX] [--no-cache] "
	             "[--out FILE] <file.spv | directory>...\n");
}

} // namespace

int main(int argc, char** argv) {
	std::filesystem::path child_path;
	std::vector<std::filesystem::path> inputs;
	std::filesystem::path output_path;
	uint64_t timeout_seconds = 20;
	uint64_t repeat = 1;
	uint64_t device = 0;
	int device_index = -1;
	bool no_cache = false;
	bool device_selected = false;
	const std::string executable = ExecutablePath(argc > 0 ? argv[0] : "kyty_spvbench");
	for (int i = 1; i < argc; i++) {
		const std::string_view argument = argv[i];
		uint64_t* number = nullptr;
		if (argument == "--timeout") number = &timeout_seconds;
		if (argument == "--repeat") number = &repeat;
		if (argument == "--device") {
			number = &device;
			device_selected = true;
		}
		if (number != nullptr) {
			if (++i >= argc || !ParseUnsigned(argv[i], *number)) {
				Usage();
				return 2;
			}
			continue;
		}
		if (argument == "--child") {
			if (++i >= argc) {
				Usage();
				return 2;
			}
			child_path = argv[i];
		} else if (argument == "--no-cache") {
			no_cache = true;
		} else if (argument == "--out") {
			if (++i >= argc) {
				Usage();
				return 2;
			}
			output_path = argv[i];
		} else if (argument == "--help" || argument == "-h") {
			Usage();
			return 0;
		} else if (!argument.empty() && argument.front() == '-') {
			Usage();
			return 2;
		} else {
			inputs.emplace_back(argument);
		}
	}
	if (device > static_cast<uint64_t>(INT32_MAX)) {
		Usage();
		return 2;
	}
	if (device_selected) device_index = static_cast<int>(device);
	if (!child_path.empty()) return RunChild(child_path, device_index, no_cache);
	if (inputs.empty() || timeout_seconds == 0 || timeout_seconds > UINT32_MAX / 1000u || repeat == 0 || repeat > 100000) {
		Usage();
		return 2;
	}
	if (output_path.empty()) output_path = std::filesystem::path(executable).parent_path() / "spvbench.txt";

	std::vector<std::filesystem::path> files;
	std::error_code ec;
	for (const auto& input: inputs) {
		if (std::filesystem::is_directory(input, ec)) {
			std::vector<std::filesystem::path> directory_files;
			for (std::filesystem::directory_iterator it(input, ec), end; !ec && it != end; it.increment(ec)) {
				if (it->is_regular_file(ec) && it->path().extension() == ".spv") directory_files.push_back(it->path());
			}
			std::ranges::sort(directory_files);
			files.insert(files.end(), directory_files.begin(), directory_files.end());
			ec.clear();
		} else {
			files.push_back(input);
		}
	}

	VulkanSession probe;
	const VkResult probe_result = probe.Init(device_index);
	if (probe_result != VK_SUCCESS) {
		std::fprintf(stderr, "SPVBENCH device initialization failed: %s(%d)\n", ResultName(probe_result),
		             static_cast<int>(probe_result));
		return 1;
	}
	std::ofstream output(output_path, std::ios::app);
	if (!output) {
		std::fprintf(stderr, "SPVBENCH cannot append to %s\n", output_path.string().c_str());
		return 1;
	}
	const std::string device_line = probe.Description();
	std::printf("%s\n", device_line.c_str());
	output << device_line << '\n';
	output.flush();
	probe.Reset();
	const uint32_t timeout_ms = static_cast<uint32_t>(timeout_seconds * 1000u);
	for (const auto& file: files) {
		for (uint64_t iteration = 0; iteration < repeat; iteration++) {
			ChildResult child = RunIsolated(executable, file, device_index, no_cache, timeout_ms);
			uint64_t bytes = 0;
			std::error_code size_error;
			bytes = std::filesystem::file_size(file, size_error);
			if (size_error) bytes = 0;
			std::string line;
			if (child.timeout) {
				char text[1024];
				std::snprintf(text, sizeof(text), "SPVBENCH file=%s bytes=%" PRIu64 " result=TIMEOUT ms=%u",
				              file.string().c_str(), bytes, timeout_ms);
				line = text;
			} else if (child.abnormal) {
				char text[1024];
				std::snprintf(text, sizeof(text), "SPVBENCH file=%s bytes=%" PRIu64 " result=CRASH code=0x%08" PRIx64
				                                 " ms=%" PRIu64,
				              file.string().c_str(), bytes, child.code, child.ms);
				line = text;
			} else {
				line = std::move(child.output);
				Trim(line);
				if (!line.starts_with("SPVBENCH ")) {
					char text[128];
					std::snprintf(text, sizeof(text), "SPVBENCH file=%s bytes=%" PRIu64 " result=CRASH code=0x0 ms=%" PRIu64,
					              file.string().c_str(), bytes, child.ms);
					line = text;
				}
			}
			std::printf("%s\n", line.c_str());
			std::fflush(stdout);
			output << line << '\n';
			output.flush();
		}
	}
	return 0;
}
