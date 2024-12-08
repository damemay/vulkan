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
#include <stdexcept>
#include <vector>
#include <vk_mem_alloc.h>

namespace vki {

enum class err {
    ok = 1,
    instance_create,
    debug_create,
    surface_create,
    no_gpu,
    no_extension_support,
    no_queues,
    device_create,
    swapchain_create,
    buffer_create,
    image_create,
};

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
};

struct base {
    vk::Instance instance = nullptr;
    vk::DebugUtilsMessengerEXT debug = nullptr;
    vk::SurfaceKHR surface = nullptr;
    vk::PhysicalDevice pdev = nullptr;
    vk::Device dev = nullptr;

    base() = default;
    ~base();

    err create_instance(uint32_t version, bool debug);
    err create_surface(GLFWwindow *window);
    err create_device(dev_info *info);

    operator vk::Instance() { return instance; }
    operator vk::Instance &() { return instance; }
    operator VkInstance() { return (VkInstance)instance; }

    operator vk::Device() { return dev; }
    operator vk::Device &() { return dev; }
    operator VkDevice() { return (VkDevice)dev; }
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

    swp(swp_init_info info, vk::Device dev);
    err create(vk::PhysicalDevice pdev, vk::SurfaceKHR surface);
    err recreate(vk::Extent2D new_extent, vk::PhysicalDevice pdev, vk::SurfaceKHR surface);
    ~swp();

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

    err create(vk::BufferCreateInfo buf_info, VmaAllocationCreateInfo alloc_info,
               VmaAllocator alloc);

    operator vk::Buffer() { return buffer; }
    operator vk::Buffer &() { return buffer; }
    operator VkBuffer() { return (VkBuffer)buffer; }
};

struct img {
    vk::Image image;
    vk::ImageView imageview;
    VmaAllocation allocation;

    vk::Extent3D extent;
    vk::Format format;
    uint32_t mip_level = 1;

    vk::Device dev;
    VmaAllocator allocator;

    img() = default;
    ~img();

    err create(vk::ImageCreateInfo img_info, vk::ImageViewCreateInfo imgv_info,
               VmaAllocationCreateInfo alloc_info, vk::Device dev, VmaAllocator alloc);

    operator vk::Image() { return image; }
    operator vk::Image &() { return image; }
    operator VkImage() { return (VkImage)image; }
    operator vk::ImageView() { return imageview; }
    operator vk::ImageView &() { return imageview; }
    operator VkImageView() { return (VkImageView)imageview; }
};
} // namespace vki
