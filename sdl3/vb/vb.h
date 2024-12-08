#pragma once

#define VK_NO_PROTOTYPES
#include <string>
#include <functional>
#include <optional>
#include <span>
#include <assert.h>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <volk.h>

namespace vb {
    /**
     * Print to `stderr` with `vb:` prefixed.
     */
    static inline void log(const std::string& buf) {
	fprintf(stderr, "vb: %s\n", buf.c_str());
    }

    static inline VKAPI_ATTR VkBool32 
	VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
	    if(data->pMessage) log(data->pMessage);
	    return VK_FALSE;
	}

    const VkDebugUtilsMessengerCreateInfoEXT ContextDebugUtilsInfo = {
      	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
     	    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
 	    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
	    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
	    | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
	    | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT
	    | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
       .pfnUserCallback = debug_callback,
    };

    /**
     * Enumeration of queue family types to query.
     */
    enum struct Queue {
	Graphics,
	Compute,
	Transfer,
	Present
    };

    /**
     * Structure containing `VkQueue` and it's family information.
     */
    struct QueueIndex {
	Queue type;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t index = 0;
	static VkQueueFlags queue_to_flag(const Queue& queue);
    };

    /**
     * Structure configuring `SDL_Window` and `VkInstance`.
     */
    struct ContextInstanceWindowInfo {
        std::string title = "vbc";
        uint32_t width = 800;
        uint32_t height = 600;
        SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN;
	uint32_t vulkan_api = VK_API_VERSION_1_0;
	VkInstanceCreateFlags instance_flags;
	std::vector<std::string> required_extensions;
	std::vector<std::string> optional_extensions;
	std::vector<std::string> required_layers;
	std::vector<std::string> optional_layers;
	void* pNext = nullptr;

	/**
	 * Set fields to require debug options.
	 */
	void require_debug() {
	    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	    required_layers.push_back("VK_LAYER_KHRONOS_validation");
	    pNext = (void*)&ContextDebugUtilsInfo;
	}

	/**
	 * Set fields to ask for debug options.
	 */
	void opt_for_debug() {
	    optional_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	    optional_layers.push_back("VK_LAYER_KHRONOS_validation");
	    pNext = (void*)&ContextDebugUtilsInfo;
	}

	/**
	 * Set fields to ask for portability enumeration extensions.
	 */
	void opt_for_portability() {
	    instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	    optional_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	}
    };

    /**
     * Structure configuring `VkPhysicalDevice` choice, `VkDevice` and `VkQueue`s to create/look for.
     */
    struct ContextDeviceInfo {
	VkPhysicalDeviceType preferred_device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	std::vector<Queue> queues_to_request = {Queue::Graphics};

        std::vector<std::string> required_extensions;
        std::vector<std::string> optional_extensions;
    
        VkPhysicalDeviceFeatures vk10features;
        VkPhysicalDeviceVulkan11Features vk11features;
        VkPhysicalDeviceVulkan12Features vk12features;
        VkPhysicalDeviceVulkan13Features vk13features;
    };

    /**
     * Structure configuring `VkSwapchainKHR`.
     */
    struct ContextSwapchainInfo {
	uint32_t width = 800;
	uint32_t height = 600;
        VkSurfaceFormatKHR surface_format = {
	    VK_FORMAT_B8G8R8A8_SRGB,
	    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
	};
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    };

    /**
     * Structure containing all basic `Vulkan` and `SDL3` handles.
     */
    struct Context {
	SDL_Window* window = nullptr;
	VmaAllocator allocator = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchain_format;
	VkExtent2D swapchain_extent;
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_image_views;
	struct SwapchainSupportData {
	    VkSurfaceFormatKHR format;
	    VkPresentModeKHR present_mode;
	    VkSurfaceCapabilitiesKHR surface_capabilities;
	    uint32_t image_count;
	    VkSharingMode image_sharing_mode;
	    std::vector<uint32_t> queue_family_indices;
	};
	SwapchainSupportData swapchain_support_data;
	std::vector<QueueIndex> queues;
	std::function<void()> resize_callback = nullptr;
	struct CommandSubmitter {
	    VkQueue queue;
	    uint32_t index;
	    VkFence fence = VK_NULL_HANDLE;
	    VkCommandBuffer buffer = VK_NULL_HANDLE;
	};
	std::optional<CommandSubmitter> command_submitter = std::nullopt;

	[[nodiscard]] Context() {};
	~Context();

	/**
	 * Initialize `SDL` and `Volk`.
	 */
	bool init(SDL_InitFlags flags = SDL_INIT_VIDEO);

	/**
	 * Create `VkInstance` and `SDL_Window`.
	 */
	bool create_instance_window(ContextInstanceWindowInfo& info);

	/**
	 * Pick `VkPhysicalDevice` and create `VkDevice`.
	 */
	bool create_device(ContextDeviceInfo& info);

	/**
	 * Create `VkSurfaceKHR`, `VkSwapchainKHR`, it's `VkImage`s and `VkImageView`s.
	 */
	bool create_surface_swapchain(ContextSwapchainInfo& info);

	/**
	 * Initialize VulkanMemoryAllocator handle.
	 */
	bool init_vma(VmaAllocatorCreateFlags flags = 0);

	/**
	 * Initialize immediate command submitter structure.
	 *
	 * @param cmd Allocated `VkCommandBuffer` that will be used.
	 * @param queue `VkQueue` to submit to.
	 * @param queue_index Index of queue to submit to.
	 */
	bool init_command_submitter(VkCommandBuffer cmd, VkQueue queue, uint32_t queue_index);

	/**
	 * Submit command with immediate command submitter.
	 *
	 * @param fn Lambda or function that records into already begun command buffer.
	 */
	bool submit_command_to_queue(std::function<void(VkCommandBuffer cmd)>&& fn);

	/**
	 * Get a pointer to one of created queues.
	 *
	 * @param type Type of queue to get.
	 */
	QueueIndex* find_queue(const Queue& type);

	/**
	 * Acquire next `VkSwapchainKHR` image.
	 *
	 * @param signal_semaphore Semaphore that will be signaled on acquisition.
	 */
	[[nodiscard]] std::optional<uint32_t> acquire_next_image(VkSemaphore signal_semaphore);

	/**
	 * Set `resize_callback` (called on `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR` in `acquire_next_image`.
	 */
	void set_resize_callback(std::function<void()>&& fn) {
	    resize_callback = fn; 
	}

	/**
	 * Recreate `VkSwapchainKHR`, it's `VkImage`s and `VkImageView`s to new `SDL_Window` size.
	 *
	 * @param call_before_swapchain_create Lambda or function to be called between destroying and recreating the swapchain.
	 */
	void recreate_swapchain(std::function<void(uint32_t,uint32_t)>&&
		call_before_swapchain_create = nullptr);

	protected:
	    bool create_swapchain_image_views();
	    void destroy_swapchain();
    };

    /**
     * Transition `VkImage` from `old_layout` to `new_layout`.
     */
    void transition_image(VkCommandBuffer cmd, VkImage image,
	    VkImageLayout old_layout, VkImageLayout new_layout);

    /**
     * Blit source `VkImage` to `dest`.
     */
    void blit_image(VkCommandBuffer cmd, VkImage source, VkImage dest,
	    VkExtent3D src_extent, VkExtent3D dst_extent, uint32_t mip_level = 0,
	    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT);

    [[nodiscard]] VkCommandPool create_cmd_pool(VkDevice device,
	    uint32_t queue_family_index, VkCommandPoolCreateFlags flags);
    [[nodiscard]] VkSemaphore create_semaphore(VkDevice device, VkSemaphoreCreateFlags flags = 0);
    [[nodiscard]] VkFence create_fence(VkDevice device, VkFenceCreateFlags flags = 0);
    [[nodiscard]] VkShaderModule create_shader_module(VkDevice device, const char* path);

    struct ContextDependant { Context* ctx; };
    struct OptionalValidator { virtual bool all_valid() = 0; };

    /**
     * `VkCommandPool` helper.
     */
    struct CommandPool : public ContextDependant, public OptionalValidator {
	VkCommandPool pool = VK_NULL_HANDLE;
	uint32_t queue_index = 0;
	bool all_valid() {return pool;}

	[[nodiscard]] CommandPool(Context* context): ContextDependant{context} {}

	/**
	 * Create `VkCommandPool`.
	 *
	 * @param queue_index Index of queue family on which the pool is created.
	 * @param flags `VkCommandPoolCreateFlags` bits. Defaults to `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`.
	 */
	void create(uint32_t queue_index,
		VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	[[nodiscard]] VkCommandBuffer allocate();
	void clean();
    };

    /**
     * `VkDescriptorPool`, `VkDescriptorSet` and `VkDescriptorSetLayout` helper.
     */
    struct DescriptorPool : public ContextDependant, public OptionalValidator {
	VkDescriptorPool pool = VK_NULL_HANDLE;
	bool all_valid() { return pool; }

	std::vector<VkDescriptorSetLayoutBinding> bindings;

	[[nodiscard]] DescriptorPool(Context* context): ContextDependant{context} {}

	/**
	 * Adds `VkDescriptorSetLayoutBinding` to use in `VkDescriptorSetLayout` creation.
	 *
	 * @param type Desciptor's type.
	 * @param stage Shader's stage.
	 * @param binding Binding index.
	 * @param count Descriptors count. Defaults to `1`.
	 */
	void add_binding(VkDescriptorType type, VkShaderStageFlags stage,
	    uint32_t binding, uint32_t count = 1);

	/**
	 * Creates `VkDescriptorPool`.
	 *
	 * @param sizes Accepts `std::span` of `VkDescriptorPoolSize`.
	 * @param max_sets Max number of sets.
	 * @param flags `VkDescriptorPoolCreateFlags` bits.
	 */
	void create(std::span<VkDescriptorPoolSize> sizes,
	    uint32_t max_sets, VkDescriptorPoolCreateFlags flags = 0);

	/**
	 * Creates `VkDescriptorSet` from this pool.
	 *
	 * @param layout `VkDescriptorSetLayout` that the set is used with.
	 * @param count Sets count. Defaults to `1`.
	 * @param pNext `pNext` pointer. Defaults to `nullptr`.
	 */
	[[nodiscard]] VkDescriptorSet create_set(VkDescriptorSetLayout layout,
		uint32_t count = 1, void* pNext = nullptr);

	/**
	 * Creates `VkDescriptorSetLayout` based on bindings added with `add_binding`.
	 *
	 * @param flags `VkDescriptorSetLayoutCreateFlags` bits.
	 * @param pNext `pNext` pointer. Defaults to `nullptr`.
	 */
	[[nodiscard]] VkDescriptorSetLayout create_layout(VkDescriptorSetLayoutCreateFlags flags = 0,
		void* pNext = nullptr);

	/**
	 * Destroys `VkDescriptorPool` and cleans bindings vector.
	 */
	void clean();

	/**
	 * Cleans bindings vector.
	 */
	void clean_bindings();

	/**
	 * Destroys `VkDescriptorSetLayout`.
	 */
	void clean_layout(VkDescriptorSetLayout& layout);
    };

    /**
     * `VkBuffer` helper.
     */
    struct Buffer: public ContextDependant, public OptionalValidator {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VmaAllocationInfo info;
	bool all_valid() { return buffer && allocation; }

	[[nodiscard]] Buffer(Context* context): ContextDependant{context} {}

	/**
	 * Creates new `VkBuffer`.
	 */
	void create(const size_t size, VkBufferCreateFlags usage, VmaMemoryUsage mem_usage);

	/**
	 * Destroys `VkBuffer` and `VmaAllocation`.
	 */
	void clean();
    };

    /**
     * `VkImage` helper.
     */
    struct Image: public ContextDependant, public OptionalValidator {
	VkImage image = VK_NULL_HANDLE;
	VkImageView image_view = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	bool all_valid() { return image && image_view && allocation; }

	VkExtent3D extent;
	VkFormat format;
	uint32_t mip_level = 1;
	[[nodiscard]] Image(Context* context): ContextDependant{context} {}

	/**
	 * Creates new `VkImage`.
	 *
	 * @param extent Image's dimensions in `VkExtent3D`.
	 * @param mipmap Sets `mipLevels`. Defaults to `false`.
	 * @param samples Sets `samples`. Defaults to `VK_SAMPLE_COUNT_1_BIT`.
	 * @param format `VkFormat` of image. Defaults to `VK_FORMAT_B8G8R8A8_SRGB`.
	 * @param usage `VkImageUsageFlags` bits. Defaults to `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
	 */
	void create(VkExtent3D extent, bool mipmap = false,
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
		VkFormat format = VK_FORMAT_B8G8R8A8_SRGB,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		| VK_IMAGE_USAGE_STORAGE_BIT  | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		| VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	/**
	 * Creates new `VkImage` from data pointer.
	 *
	 * @param data `void*` to image data that will be copied into `VkImage` by staging `VkBuffer`.
	 * @param extent Image's dimensions in `VkExtent3D`.
	 * @param mipmap Sets `mipLevels`. Defaults to `false`.
	 * @param samples Sets `samples`. Defaults to `VK_SAMPLE_COUNT_1_BIT`.
	 * @param format `VkFormat` of image. Defaults to `VK_FORMAT_B8G8R8A8_SRGB`.
	 * @param usage `VkImageUsageFlags` bits. Defaults to `VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
	 */
	void create(void* data, VkExtent3D extent, bool mipmap = false,
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	/**
	 * Creates new `VkImage` from file with `stb_image`. Calls `vb::Image::create(void*, VkExtent3D, format, usage, mipmap)` internally.
	 *
	 * @param path Path to image file.
	 * @param mipmap Sets `mipLevels`. Defaults to `false`.
	 * @param samples Sets `samples`. Defaults to `VK_SAMPLE_COUNT_1_BIT`.
	 * @param format `VkFormat` of image. Defaults to `VK_FORMAT_B8G8R8A8_SRGB`.
	 * @param usage `VkImageUsageFlags` bits. Defaults to `VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
	 */
	void create(const char* path, bool mipmap = false,
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	/**
	 * Destroys `VkImage`, `VkImageView` and `VmaAllocation`.
	 */
	void clean();
    };

    /**
     * `VkPipeline` helper for graphics pipeline.
     *
     * `VkDynamicState` defaults are `VK_DYNAMIC_STATE_VIEWPORT` and `VK_DYNAMIC_STATE_SCISSOR`.
     *
     * `VkPipelineInputAssemblyStateCreateInfo` defauls to `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` and `primitiveRestart` disabled.
     *
     * `VkPipelineRasterizationStateCreateInfo` defaults to `VK_POLYGON_MODE_FILL`, `VK_CULL_MODE_BACK_BIT` and `VK_FRONT_FACE_CLOCKWISE`.
     *
     * `VkPipelineMultisampleStateCreateInfo` defaults to disabled sample shading with `rasterizationSamples` set to `VK_SAMPLE_COUNT_1_BIT`.
     *
     * `VkPipelineDepthStencilStateCreateInfo` defaults to disabled depth and stencil testing.
     *
     * `VkPipelineColorBlendStateCreateInfo` defaults to disabled `logicOp` with 1 attachment (disabled blending, and `colorWriteMask` of `VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT`.
     */
    struct GraphicsPipeline: public ContextDependant, public OptionalValidator {
       	VkPipelineVertexInputStateCreateInfo vertex_input = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	    .primitiveRestartEnable = VK_FALSE,
	};
	VkPipelineTessellationStateCreateInfo tessellation = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
	};
	VkPipelineViewportStateCreateInfo viewport = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	    .viewportCount = 1,
	    .scissorCount = 1,
	};
	VkPipelineRasterizationStateCreateInfo rasterization = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	    .polygonMode = VK_POLYGON_MODE_FILL,
	    .cullMode = VK_CULL_MODE_BACK_BIT,
	    .frontFace = VK_FRONT_FACE_CLOCKWISE,
	    .lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo multisample = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
	    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	    .sampleShadingEnable = VK_FALSE,
	    .minSampleShading = 1.0f,
	};
	VkPipelineDepthStencilStateCreateInfo depth_stencil = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	    .depthCompareOp = VK_COMPARE_OP_LESS,
	    .minDepthBounds = 0.0f,
	    .maxDepthBounds = 1.0f,
	};
	VkPipelineColorBlendAttachmentState color_blend_attachment = {
	    .blendEnable = VK_FALSE,
	    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	VkPipelineColorBlendStateCreateInfo color_blend = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
	    .logicOpEnable = VK_FALSE,
	    .logicOp = VK_LOGIC_OP_COPY,
	    .attachmentCount = 1,
	    .pAttachments = &color_blend_attachment,
	};

	std::vector<VkShaderModule> shader_modules;
	std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
	std::vector<VkPushConstantRange> push_constants;
	std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
	std::vector<VkDynamicState> dynamic_states = {
	    VK_DYNAMIC_STATE_VIEWPORT,
	    VK_DYNAMIC_STATE_SCISSOR
	};
	VkRenderPass render_pass = VK_NULL_HANDLE;
	uint32_t subpass_index = 0;

	[[nodiscard]] GraphicsPipeline(Context* context): ContextDependant{context} {}

	/**
	 * Sets the `VkRenderPass` for `VkPipeline`.
	 */
	void set_render_pass(VkRenderPass renderpass) { render_pass = renderpass; }

	/**
	 * Sets the index for `subpass` in `VkGraphicsPipelineCreateInfo`. Defaults to `0`.
	 */
	void set_subpass_index(uint32_t index) { subpass_index = index; }
	
	/**
	 * Adds a shader from `VkShaderModule`.
	 */
	void add_shader(VkShaderModule& shader_module, VkShaderStageFlagBits stage);

	/**
	 * Loads and creates a shader from `path`.
	 */
	void add_shader(const char* path, VkShaderStageFlagBits stage);
	
	/**
	 * Adds a `VkPushConstantRange`.
	 */
	void add_push_constant(const uint32_t size, VkShaderStageFlagBits stage,
		const uint32_t offset = 0);

	/**
	 * Adds a `VkDescriptorSetLayout`.
	 */
	void add_descriptor_set_layout(VkDescriptorSetLayout descriptor_set_layout) {
	    descriptor_set_layouts.push_back(descriptor_set_layout); 
	}
	
	void set_topology(VkPrimitiveTopology topology) { input_assembly.topology = topology; }
	void set_polygon_mode(VkPolygonMode mode) { rasterization.polygonMode = mode; }
	void set_cull_mode(VkCullModeFlags mode) { rasterization.cullMode = mode; }
	void set_front_face(VkFrontFace face) { rasterization.frontFace = face; }

	/**
	 * Sets `rasterizationSamples` to `count`.
	 */
	void set_sample_count(VkSampleCountFlagBits count) { multisample.rasterizationSamples = count; }

	/**
	 * Sets `sampleShadingEnable` to `VK_TRUE` and `minSampleShading` to `min_sample` (default `1.0f`).
	 */
	void enable_sample_shading(float min_sample = 1.0f) { multisample.sampleShadingEnable = VK_TRUE; multisample.minSampleShading = min_sample; }

	/**
	 * Sets both `depthTestEnable` and `depthWriteEnable` to `VK_TRUE`.
	 */
	void enable_depth_test() { depth_stencil.depthTestEnable = VK_TRUE; depth_stencil.depthWriteEnable = VK_TRUE; }
	void set_depth_comparison(VkCompareOp operation) { depth_stencil.depthCompareOp = operation; }

	/**
	 * Sets `depthBoundsTestEnable` to `VK_TRUE`.
	 */
	void enable_depth_bounds_test() { depth_stencil.depthBoundsTestEnable = VK_TRUE; }
	void set_depth_bounds(float min, float max) { depth_stencil.minDepthBounds = min; depth_stencil.maxDepthBounds = max; }

	/**
	 * Sets `stencilTestEnable` to `VK_TRUE`.
	 */
	void enable_stencil_test() { depth_stencil.stencilTestEnable = VK_TRUE; }
	void set_stencil_operations(VkStencilOpState front, VkStencilOpState back) { depth_stencil.front = front; depth_stencil.back = back; }

	/**
	 * Sets `blendEnable` to `VK_TRUE` and configures `VkPipelineColorBlendAttachmentState`.
	 *
	 * @param src_color `VkBlendFactor` of `srcColorBlendFactor`. Defaults to `VK_BLEND_FACTOR_SRC_ALPHA`.
	 * @param dst_color `VkBlendFactor` of `dstColorBlendFactor`. Defaults to `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA`.
	 * @param op_color `VkBlendOp` of `colorBlendOp`. Defaults to `VK_BLEND_OP_ADD`.
	 * @param src_alpha `VkBlendFactor` of `srcAlphaBlendFactor`. Defaults to `VK_BLEND_FACTOR_ONE`.
	 * @param dst_alpha `VkBlendFactor` of `dstAlphaBlendFactor`. Defaults to `VK_BLEND_FACTOR_ZERO`.
	 * @param op_alpha `VkBlendOp` of `alphaBlendOp`. Defaults to `VK_BLEND_OP_ADD`.
	 */
	void enable_blend(VkBlendFactor src_color = VK_BLEND_FACTOR_SRC_ALPHA,
		VkBlendFactor dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		VkBlendOp op_color = VK_BLEND_OP_ADD,
		VkBlendFactor src_alpha = VK_BLEND_FACTOR_ONE,
		VkBlendFactor dst_alpha = VK_BLEND_FACTOR_ZERO,
		VkBlendOp op_alpha = VK_BLEND_OP_ADD) {
	    color_blend_attachment.blendEnable = VK_TRUE;
	    color_blend_attachment.srcColorBlendFactor = src_color;
	    color_blend_attachment.dstColorBlendFactor = dst_color;
	    color_blend_attachment.colorBlendOp = op_color;
	    color_blend_attachment.srcAlphaBlendFactor = src_alpha;
	    color_blend_attachment.dstAlphaBlendFactor = dst_alpha;
	    color_blend_attachment.alphaBlendOp = op_alpha;
	}
	
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	bool all_valid() { return layout && pipeline; }
	
	/**
	 * Creates `VkPipeline` and `VkPipelineLayout`.
	 */
	void create(void* pNext = nullptr, VkPipelineCreateFlags flags = 0);

	/**
	 * Destroys `VkPipeline` and `VkPipelineLayout`.
	 *
	 * Does not destroy loaded `VkShaderModule`s!
	 */
	void clean();

	/**
	 * Destroys `VkShaderModule`s from vector.
	 */
	void clean_shaders();
    };
}

