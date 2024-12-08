#pragma once

#include <memory>
#include <vb.h>

namespace vb {
    using UniqueContext = std::unique_ptr<Context>;

    inline UniqueContext create_unique_context() {
	return std::make_unique<Context>();
    }
    
    inline UniqueContext create_unique_context(ContextInstanceWindowInfo& instance_window_info,
	    ContextDeviceInfo& device_info, ContextSwapchainInfo& swapchain_info,
	    VmaAllocationCreateFlags vma_flags = 0, SDL_InitFlags sdl_flags = SDL_INIT_VIDEO) {
	auto ptr = create_unique_context();
	if(!ptr->init(sdl_flags)) return nullptr;
	if(!ptr->create_instance_window(instance_window_info)) return nullptr;
	if(!ptr->create_device(device_info)) return nullptr;
	if(!ptr->create_surface_swapchain(swapchain_info)) return nullptr;
	if(!ptr->init_vma(vma_flags)) return nullptr;
	return ptr;
    }

    struct SmartCommandPool : public CommandPool {
	SmartCommandPool(Context* context): CommandPool{context} {}
	~SmartCommandPool() { clean(); }
    };
    using UniqueCommandPool = std::unique_ptr<SmartCommandPool>;
    using SharedCommandPool = std::shared_ptr<SmartCommandPool>;

    inline UniqueCommandPool create_unique_command_pool(Context* context) {
	return std::make_unique<SmartCommandPool>(context);
    }

    inline UniqueCommandPool create_unique_command_pool(Context* context, uint32_t queue_index,
    	    VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT) {
	auto ptr = std::make_unique<SmartCommandPool>(context);
	ptr->create(queue_index, flags);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline SharedCommandPool create_shared_command_pool(Context* context) {
	return std::make_shared<SmartCommandPool>(context);
    }

    inline SharedCommandPool create_shared_command_pool(Context* context, uint32_t queue_index,
    	    VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT) {
	auto ptr = std::make_shared<SmartCommandPool>(context);
	ptr->create(queue_index, flags);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    struct SmartDescriptorPool : public DescriptorPool {
	SmartDescriptorPool(Context* context): DescriptorPool{context} {}
	~SmartDescriptorPool() { clean(); }
    };
    using UniqueDescriptorPool = std::unique_ptr<SmartDescriptorPool>;
    using SharedDescriptorPool = std::shared_ptr<SmartDescriptorPool>;

    inline UniqueDescriptorPool create_unique_descriptor_pool(Context* context) {
	return std::make_unique<SmartDescriptorPool>(context);
    }

    inline UniqueDescriptorPool create_unique_descriptor_pool(Context* context,
	    std::span<VkDescriptorPoolSize> sizes, uint32_t max_sets,
	    VkDescriptorPoolCreateFlags flags = 0) {
	auto ptr = std::make_unique<SmartDescriptorPool>(context);
	ptr->create(sizes, max_sets, flags);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline SharedDescriptorPool create_shared_descriptor_pool(Context* context) {
	return std::make_shared<SmartDescriptorPool>(context);
    }

    inline SharedDescriptorPool create_shared_descriptor_pool(Context* context,
	    std::span<VkDescriptorPoolSize> sizes, uint32_t max_sets,
	    VkDescriptorPoolCreateFlags flags = 0) {
	auto ptr = std::make_shared<SmartDescriptorPool>(context);
	ptr->create(sizes, max_sets, flags);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    struct SmartBuffer : public Buffer {
	SmartBuffer(Context* context): Buffer{context} {}
	~SmartBuffer() { clean(); }
    };
    using UniqueBuffer = std::unique_ptr<SmartBuffer>;
    using SharedBuffer = std::shared_ptr<SmartBuffer>;

    inline UniqueBuffer create_unique_buffer(Context* context) {
	return std::make_unique<SmartBuffer>(context);
    }

    inline UniqueBuffer create_unique_buffer(Context* context, const size_t size,
	    VkBufferCreateFlags usage, VmaMemoryUsage mem_usage) {
	auto ptr = std::make_unique<SmartBuffer>(context);
	ptr->create(size, usage, mem_usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline SharedBuffer create_shared_buffer(Context* context) {
	return std::make_shared<SmartBuffer>(context);
    }

    inline SharedBuffer create_shared_buffer(Context* context, const size_t size,
	    VkBufferCreateFlags usage, VmaMemoryUsage mem_usage) {
	auto ptr = std::make_shared<SmartBuffer>(context);
	ptr->create(size, usage, mem_usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    struct SmartImage : public Image {
	SmartImage(Context* context): Image{context} {}
	~SmartImage() { clean(); }
    };
    using UniqueImage = std::unique_ptr<SmartImage>;
    using SharedImage = std::shared_ptr<SmartImage>;

    inline UniqueImage create_unique_image(Context* context) {
	return std::make_unique<SmartImage>(context);
    }

    inline UniqueImage create_unique_image(Context* context, VkExtent3D extent,
	    bool mipmap = false,
    	    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
	    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB,
	    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
	    | VK_IMAGE_USAGE_STORAGE_BIT  | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	    | VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
	auto ptr = std::make_unique<SmartImage>(context);
	ptr->create(extent, mipmap, samples, format, usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline UniqueImage create_unique_image(Context* context, void* data,
	    VkExtent3D extent, bool mipmap = false,
    	    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
	    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
	    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
	    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
	auto ptr = std::make_unique<SmartImage>(context);
	ptr->create(data, extent, mipmap, samples, format, usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline UniqueImage create_unique_image(Context* context, const char* path,
	    bool mipmap = false,
    	    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
	    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
	    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
	    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
	auto ptr = std::make_unique<SmartImage>(context);
	ptr->create(path, mipmap, samples, format, usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline SharedImage create_shared_image(Context* context) {
	return std::make_shared<SmartImage>(context);
    }

    inline SharedImage create_shared_image(Context* context, VkExtent3D extent,
	    bool mipmap = false,
    	    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
	    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB,
	    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
	    | VK_IMAGE_USAGE_STORAGE_BIT  | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	    | VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
	auto ptr = std::make_shared<SmartImage>(context);
	ptr->create(extent, mipmap, samples, format, usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline SharedImage create_shared_image(Context* context, void* data,
	    VkExtent3D extent, bool mipmap = false,
    	    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
	    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
	    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
	    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
	auto ptr = std::make_shared<SmartImage>(context);
	ptr->create(data, extent, mipmap, samples, format, usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }

    inline SharedImage create_shared_image(Context* context, const char* path,
	    bool mipmap = false,
    	    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
	    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
	    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
	    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
	auto ptr = std::make_shared<SmartImage>(context);
	ptr->create(path, mipmap, samples, format, usage);
	if(!ptr->all_valid()) return nullptr;
	return ptr;
    }
}
