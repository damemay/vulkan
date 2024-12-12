#include "vki.h"
#include <algorithm>
#include <fstream>
#include <set>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vki {
static inline VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT s, VkDebugUtilsMessageTypeFlagsEXT t,
               const VkDebugUtilsMessengerCallbackDataEXT *d, void *u) {
    if (d)
        if (d->pMessage)
            fprintf(stderr, "%s\n", d->pMessage);
    return VK_FALSE;
}

VmaAllocator init_vma(VmaAllocationCreateFlags flags, vk::PhysicalDevice pdev, vk::Device dev,
                      vk::Instance instance) {
    VmaVulkanFunctions f = {
        .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo info = {
        .flags = flags,
        .physicalDevice = (VkPhysicalDevice)pdev,
        .device = (VkDevice)dev,
        .pVulkanFunctions = &f,
        .instance = (VkInstance)instance,
    };

    VmaAllocator alloc = nullptr;
    vmaCreateAllocator(&info, &alloc);

    return alloc;
}

base::~base() {
    if (dev)
        dev.destroy();
    if (surface)
        instance.destroySurfaceKHR(surface);
    if (debug)
        instance.destroyDebugUtilsMessengerEXT(debug);
    if (instance)
        instance.destroy();
}

void base::create_instance(uint32_t version, bool debug) {
    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    uint32_t glfw_l = 0;
    auto glfw = glfwGetRequiredInstanceExtensions(&glfw_l);

    std::vector<const char *> exts;
    exts.reserve(glfw_l);
    for (size_t i = 0; i < glfw_l; i++)
        exts.push_back(glfw[i]);
    if (debug)
        exts.push_back(vk::EXTDebugUtilsExtensionName);

    vk::DebugUtilsMessengerCreateInfoEXT debug_i = {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
        .pfnUserCallback = debug_callback,
    };

    vk::ApplicationInfo app_i = {.apiVersion = version};
    auto [res, vki] = vk::createInstance({
        .pNext = debug ? &debug_i : nullptr,
        .pApplicationInfo = &app_i,
        .enabledExtensionCount = (uint32_t)exts.size(),
        .ppEnabledExtensionNames = exts.data(),
    });
    if (res != vk::Result::eSuccess)
        throw err::instance_create();

    instance = vki;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    if (debug) {
        auto [res, vkd] = instance.createDebugUtilsMessengerEXT(debug_i);
        if (res != vk::Result::eSuccess) {
            instance.destroy();
            throw err::debug_create();
        }
        this->debug = vkd;
    }
}

void base::create_surface(GLFWwindow *window) {
    if (glfwCreateWindowSurface((VkInstance)instance, window, nullptr, (VkSurfaceKHR *)&surface) !=
        VK_SUCCESS)
        throw err::surface_create();
}

void base::create_device(dev_info *info) {
    auto [res, vkpds] = instance.enumeratePhysicalDevices();
    if (res != vk::Result::eSuccess)
        throw err::no_gpu();

    for (auto vkpd : vkpds) {
        auto ext = vkpd.enumerateDeviceExtensionProperties().value;
        size_t count = 0;
        for (auto e : ext) {
            for (auto ue : info->device_extensions) {
                if (strcmp(e.extensionName.data(), ue) == 0)
                    count++;
            }
        }
        if (count == info->device_extensions.size()) {
            pdev = vkpd;
            break;
        }
    }
    if (!pdev)
        throw err::no_extensions();

    auto vkqfs = pdev.getQueueFamilyProperties();
    std::set<uint32_t> uqfs = {};
    size_t found_l = 0;
    for (auto &qf : info->device_queues) {
        for (size_t i = 0; i < vkqfs.size(); i++) {
            if (vkqfs[i].queueFlags & qf.flags) {
                uqfs.insert(i);
                qf.index = i;
                found_l++;

                if (qf.should_present) {
                    auto [res, can] = pdev.getSurfaceSupportKHR(i, surface);
                    qf.can_present = can;
                }
            }
        }
    }

    if (found_l == 0 || uqfs.size() == 0)
        throw err::no_queues();

    float qpr = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> qis;
    qis.reserve(uqfs.size());
    for (auto uqfi : uqfs) {
        vk::DeviceQueueCreateInfo qi = {
            .queueFamilyIndex = uqfi,
            .queueCount = 1,
            .pQueuePriorities = &qpr,
        };
        qis.push_back(qi);
    }

    info->features_13.pNext = &info->features_14;
    info->features_12.pNext = &info->features_13;
    info->features_11.pNext = &info->features_12;

    vk::DeviceCreateInfo dev_i = {
        .pNext = &info->features_11,
        .queueCreateInfoCount = (uint32_t)qis.size(),
        .pQueueCreateInfos = qis.data(),
        .enabledExtensionCount = (uint32_t)info->device_extensions.size(),
        .ppEnabledExtensionNames =
            info->device_extensions.size() > 0 ? info->device_extensions.data() : nullptr,
        .pEnabledFeatures = &info->features_10,
    };

    auto [_res, vkd] = pdev.createDevice(dev_i);
    if (_res != vk::Result::eSuccess)
        throw err::device_create();

    dev = vkd;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(dev);
}

swp::swp(vk::Device dev, vk::PhysicalDevice pdev, vk::SurfaceKHR surface, swp_init_info info) {
    surface_format = info.surface_format;
    present_mode = info.present_mode;
    this->dev = dev;

    std::set<uint32_t> uqfi;
    for (auto qf : info.queue_family_indices)
        uqfi.insert(qf);
    unique_queue_family_indices = std::vector<uint32_t>(uqfi.begin(), uqfi.end());
    sharing_mode = unique_queue_family_indices.size() > 1 ? vk::SharingMode::eConcurrent
                                                          : vk::SharingMode::eExclusive;

    auto swp_i = ci(extent, surface, pdev);

    auto [res, vkswp] = dev.createSwapchainKHR(swp_i);
    if (res != vk::Result::eSuccess)
        throw err::swapchain_create();
    swapchain = vkswp;

    get_imgs();
}

swp::~swp() {
    for (auto iv : imageviews)
        dev.destroyImageView(iv);
    dev.destroySwapchainKHR(swapchain);
}

void swp::recreate(vk::Extent2D new_extent, vk::PhysicalDevice pdev, vk::SurfaceKHR surface) {
    auto nswp_i = ci(new_extent, surface, pdev);
    auto old = swapchain;
    nswp_i.oldSwapchain = swapchain;

    auto [res, vkswp] = dev.createSwapchainKHR(nswp_i);
    if (res != vk::Result::eSuccess)
        throw err::swapchain_create();
    swapchain = vkswp;

    dev.destroySwapchainKHR(old);
    for (auto iv : imageviews)
        dev.destroyImageView(iv);
    imageviews.clear();

    get_imgs();
}

void swp::get_imgs() {
    auto [res, vksi] = dev.getSwapchainImagesKHR(swapchain);
    if (res != vk::Result::eSuccess)
        throw err::no_swapchain_img();
    images = vksi;

    imageviews.resize(images.size());
    for (size_t i = 0; i < images.size(); i++) {
        vk::ImageViewCreateInfo iv_i = {
            .image = images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = surface_format.format,
            .components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity},
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .levelCount = 1,
                                 .layerCount = 1},
        };

        auto [res, vksiv] = dev.createImageView(iv_i);
        if (res != vk::Result::eSuccess) {
            for (size_t j = 0; j < i; j++)
                dev.destroyImageView(imageviews[j]);
            throw err::image_view_create();
        }

        imageviews[i] = vksiv;
    }
}

vk::SwapchainCreateInfoKHR swp::ci(vk::Extent2D ext, vk::SurfaceKHR surface,
                                   vk::PhysicalDevice pdev) {
    auto [res, cap] = pdev.getSurfaceCapabilitiesKHR(surface);
    surface_cap = cap;

    image_count = surface_cap.minImageCount == surface_cap.maxImageCount
                      ? surface_cap.maxImageCount
                      : surface_cap.minImageCount + 1;

    if (surface_cap.currentExtent.width != UINT32_MAX)
        extent = surface_cap.currentExtent;
    else
        extent = vk::Extent2D{std::clamp(ext.width, surface_cap.minImageExtent.width,
                                         surface_cap.maxImageExtent.width),
                              std::clamp(ext.height, surface_cap.minImageExtent.height,
                                         surface_cap.maxImageExtent.height)};

    vk::SwapchainCreateInfoKHR ci = {
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
        .queueFamilyIndexCount = (uint32_t)unique_queue_family_indices.size(),
        .pQueueFamilyIndices = unique_queue_family_indices.data(),
        .preTransform = surface_cap.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = present_mode,
        .clipped = vk::True,
    };

    return ci;
}

buf::~buf() { vmaDestroyBuffer(allocator, buffer, allocation); }

void buf::create(VmaAllocator alloc, vk::BufferCreateInfo buf_info,
                 VmaAllocationCreateInfo alloc_info) {
    allocator = alloc;

    if (vmaCreateBuffer(allocator, (VkBufferCreateInfo *)&buf_info, &alloc_info,
                        (VkBuffer *)&buffer, &allocation, &this->alloc_info) != VK_SUCCESS)
        throw err::buffer_create();
}

buf::buf(VmaAllocator alloc, vk::BufferCreateInfo buf_info, VmaAllocationCreateInfo alloc_info) {
    create(alloc, buf_info, alloc_info);
}

buf::buf(VmaAllocator alloc, const size_t size, vk::BufferUsageFlags buf_usage,
         VmaMemoryUsage mem_usage) {
    vk::BufferCreateInfo buf_info = {
        .size = size,
        .usage = buf_usage,
    };

    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = mem_usage,
    };

    create(alloc, buf_info, alloc_info);
}

img::~img() {
    vmaDestroyImage(allocator, image, allocation);
    dev.destroyImageView(image_view);
}

void img::create(vk::Device dev, VmaAllocator alloc, vk::ImageCreateInfo img_info,
                 vk::ImageViewCreateInfo imgv_info, VmaAllocationCreateInfo alloc_info) {
    this->dev = dev;
    allocator = alloc;
    this->img_info = img_info;
    this->img_view_info = imgv_info;

    if (vmaCreateImage(allocator, (VkImageCreateInfo *)&img_info, &alloc_info, (VkImage *)&image,
                       &allocation, nullptr) != VK_SUCCESS)
        throw err::image_create();

    imgv_info.image = image;
    auto [res, vkiv] = dev.createImageView(imgv_info);
    if (res != vk::Result::eSuccess) {
        vmaDestroyImage(allocator, image, allocation);
        throw err::image_view_create();
    }

    image_view = vkiv;
}

img::img(vk::Device dev, VmaAllocator alloc, vk::ImageCreateInfo img_info,
         vk::ImageViewCreateInfo imgv_info, VmaAllocationCreateInfo alloc_info) {
    create(dev, alloc, img_info, imgv_info, alloc_info);
}

img::img(vk::Device dev, VmaAllocator alloc, vk::Extent2D extent, vk::ImageUsageFlags usage,
         vk::ImageAspectFlagBits aspect, vk::Format format, vk::SampleCountFlagBits samples,
         uint32_t mipmaps) {
    vk::ImageCreateInfo img_info = {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .mipLevels = mipmaps,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = usage,
    };

    vk::ImageViewCreateInfo imgv_info = {
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {.aspectMask = aspect, .levelCount = mipmaps, .layerCount = 1},
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    create(dev, alloc, img_info, imgv_info, alloc_info);
}

shm::~shm() { dev.destroyShaderModule(module); }

shm::shm(vk::Device dev, const char *path) {
    this->dev = dev;

    std::ifstream file{path, std::ios::binary};
    file.exceptions(std::ios::failbit | std::ios::badbit);
    std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});

    vk::ShaderModuleCreateInfo info = {
        .codeSize = buffer.size() * sizeof(char),
        .pCode = (uint32_t *)buffer.data(),
    };

    auto [res, vks] = dev.createShaderModule(info);
    if (res != vk::Result::eSuccess)
        throw err::shader_create();

    module = vks;
}
} // namespace vki
