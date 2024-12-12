#pragma once

#define VULKAN_HPP_NO_SPACESHIP_OPERATOR
#define VULKAN_HPP_NO_TO_STRING
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_SMART_HANDLE
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include <GLFW/glfw3.h>
#include <vector>
#include <vk_mem_alloc.h>

namespace vki {

VmaAllocator init_vma(VmaAllocationCreateFlags flags, vk::PhysicalDevice pdev, vk::Device dev,
                      vk::Instance instance);

struct queue_info {
    vk::QueueFlags flags;
    bool should_present = false;

    uint32_t index = 0;
    bool can_present = false;
};

struct dev_info {
    std::vector<queue_info> device_queues = {};
    std::vector<const char *> device_extensions = {};
    vk::PhysicalDeviceFeatures features_10 = {};
    vk::PhysicalDeviceVulkan11Features features_11 = {};
    vk::PhysicalDeviceVulkan12Features features_12 = {};
    vk::PhysicalDeviceVulkan13Features features_13 = {};
    vk::PhysicalDeviceVulkan14Features features_14 = {};
    VmaAllocationCreateFlags vma_flags = 0;
};

struct base {
    vk::Instance instance = nullptr;
    vk::DebugUtilsMessengerEXT debug = nullptr;
    vk::SurfaceKHR surface = nullptr;
    vk::PhysicalDevice pdev = nullptr;
    vk::Device dev = nullptr;
    VmaAllocator allocator = nullptr;

    base() = default;
    ~base();

    bool create(uint32_t api, bool debug, GLFWwindow *window = nullptr);
    bool check_device_extensions(std::span<const char *> extensions);
    bool check_device_queues(std::span<queue_info> queues);
    bool create_device(dev_info *info);

    operator vk::Instance() { return instance; }
    operator vk::Instance &() { return instance; }
    operator VkInstance() { return (VkInstance)instance; }

    operator vk::Device() { return dev; }
    operator vk::Device &() { return dev; }
    operator VkDevice() { return (VkDevice)dev; }

  private:
    bool create_instance(uint32_t version, bool debug);
    bool create_surface(GLFWwindow *window);
};

struct swp_init_info {
    vk::Extent2D extent = {};
    vk::SurfaceFormatKHR surface_format =
        vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
    vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
    std::vector<uint32_t> queue_family_indices = {};
};

struct swp {
    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> imageviews;
    uint32_t image_count;

    vk::Extent2D extent;
    vk::SurfaceCapabilitiesKHR surface_cap;
    vk::SurfaceFormatKHR surface_format;
    vk::PresentModeKHR present_mode;
    vk::SharingMode sharing_mode;
    std::vector<uint32_t> unique_queue_family_indices;

    vk::Device dev;

    swp() = default;
    ~swp();

    bool create(vk::Device dev, vk::PhysicalDevice pdev, vk::SurfaceKHR surface,
                swp_init_info info);
    bool recreate(vk::Extent2D new_extent, vk::PhysicalDevice pdev, vk::SurfaceKHR surface);

    operator vk::SwapchainKHR() { return swapchain; }
    operator vk::SwapchainKHR &() { return swapchain; }
    operator VkSwapchainKHR() { return (VkSwapchainKHR)swapchain; }

  private:
    vk::SwapchainCreateInfoKHR ci(vk::Extent2D ext, vk::SurfaceKHR surface,
                                  vk::PhysicalDevice pdev);
    bool get_imgs();
};

struct buf {
    vk::Buffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo alloc_info;

    VmaAllocator allocator;

    buf() = default;
    ~buf();

    bool create(VmaAllocator alloc, vk::BufferCreateInfo buf_info,
                VmaAllocationCreateInfo alloc_info);

    bool create(VmaAllocator alloc, const size_t size, vk::BufferUsageFlags buf_usage,
                VmaMemoryUsage mem_usage);

    operator vk::Buffer() { return buffer; }
    operator vk::Buffer &() { return buffer; }
    operator VkBuffer() { return (VkBuffer)buffer; }
};

struct img {
    vk::Image image;
    vk::ImageView image_view;
    VmaAllocation allocation;

    vk::ImageCreateInfo img_info;
    vk::ImageViewCreateInfo img_view_info;

    vk::Device dev;
    VmaAllocator allocator;

    img() = default;
    ~img();

    bool create(vk::Device dev, VmaAllocator alloc, vk::ImageCreateInfo img_info,
                vk::ImageViewCreateInfo imgv_info, VmaAllocationCreateInfo alloc_info);

    bool create(vk::Device dev, VmaAllocator alloc, vk::Extent2D extent, vk::ImageUsageFlags usage,
                vk::ImageAspectFlagBits aspect, vk::Format format = vk::Format::eB8G8R8A8Srgb,
                vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
                uint32_t mipmaps = 1);

    operator vk::Image() { return image; }
    operator vk::Image &() { return image; }
    operator VkImage() { return (VkImage)image; }
    operator vk::ImageView() { return image_view; }
    operator vk::ImageView &() { return image_view; }
    operator VkImageView() { return (VkImageView)image_view; }
};

struct shm {
    vk::ShaderModule module;
    vk::Device dev;

    shm();
    ~shm();

    bool create(vk::Device dev, const char *path);

    operator vk::ShaderModule() { return module; }
    operator vk::ShaderModule &() { return module; }
    operator VkShaderModule() { return (VkShaderModule)module; }
};
} // namespace vki
