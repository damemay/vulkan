#include "vki.h"
#include <algorithm>
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

err base::create_instance(uint32_t version, bool debug) {
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
        return err::instance_create;

    instance = vki;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    if (debug) {
        auto [res, vkd] = instance.createDebugUtilsMessengerEXT(debug_i);
        if (res != vk::Result::eSuccess) {
            instance.destroy();
            return err::debug_create;
        }
        this->debug = vkd;
    }

    return err::ok;
}

err base::create_surface(GLFWwindow *window) {
    if (glfwCreateWindowSurface((VkInstance)instance, window, nullptr, (VkSurfaceKHR *)&surface) !=
        VK_SUCCESS)
        return err::surface_create;
    return err::ok;
}

err base::create_device(dev_info *info) {
    auto [res, vkpds] = instance.enumeratePhysicalDevices();
    if (res != vk::Result::eSuccess)
        return err::no_gpu;

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
        return err::no_extension_support;

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
        return err::no_queues;

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
        return err::device_create;

    dev = vkd;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(dev);
    return err::ok;
}

swp::swp(swp_init_info info, vk::Device dev) {
    surface_format = info.surface_format;
    present_mode = info.present_mode;
    this->dev = dev;

    std::set<uint32_t> uqfi;
    for (auto qf : info.queue_family_indices)
        uqfi.insert(qf);
    unique_queue_family_indices = std::vector<uint32_t>(uqfi.begin(), uqfi.end());
    sharing_mode = unique_queue_family_indices.size() > 1 ? vk::SharingMode::eConcurrent
                                                          : vk::SharingMode::eExclusive;
}

err swp::create(vk::PhysicalDevice pdev, vk::SurfaceKHR surface) {
    auto swp_i = ci(extent, surface, pdev);

    auto [res, vkswp] = dev.createSwapchainKHR(swp_i);
    if (res != vk::Result::eSuccess)
        return err::swapchain_create;
    swapchain = vkswp;

    if (!get_imgs())
        return err::swapchain_create;
    return err::ok;
}

swp::~swp() {
    for (auto iv : imageviews)
        dev.destroyImageView(iv);
    dev.destroySwapchainKHR(swapchain);
}

err swp::recreate(vk::Extent2D new_extent, vk::PhysicalDevice pdev, vk::SurfaceKHR surface) {
    auto nswp_i = ci(new_extent, surface, pdev);
    auto old = swapchain;
    nswp_i.oldSwapchain = swapchain;

    auto [res, vkswp] = dev.createSwapchainKHR(nswp_i);
    if (res != vk::Result::eSuccess)
        return err::swapchain_create;
    swapchain = vkswp;

    dev.destroySwapchainKHR(old);
    for (auto iv : imageviews)
        dev.destroyImageView(iv);
    imageviews.clear();

    if (!get_imgs())
        return err::swapchain_create;
    return err::ok;
}

bool swp::get_imgs() {
    auto [res, vksi] = dev.getSwapchainImagesKHR(swapchain);
    if (res != vk::Result::eSuccess)
        return false;
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
            return false;
        }

        imageviews[i] = vksiv;
    }

    return true;
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

err buf::create(vk::BufferCreateInfo buf_info, VmaAllocationCreateInfo alloc_info,
                VmaAllocator alloc) {
    allocator = alloc;

    if (vmaCreateBuffer(allocator, (VkBufferCreateInfo *)&buf_info, &alloc_info,
                        (VkBuffer *)&buffer, &allocation, &this->alloc_info) != VK_SUCCESS)
        return err::buffer_create;
    return err::ok;
}

err img::create(vk::ImageCreateInfo img_info, vk::ImageViewCreateInfo imgv_info,
                VmaAllocationCreateInfo alloc_info, vk::Device dev, VmaAllocator alloc) {
    this->dev = dev;
    allocator = alloc;
    mip_level = img_info.mipLevels;
    format = img_info.format;
    extent = img_info.extent;

    if (vmaCreateImage(allocator, (VkImageCreateInfo *)&img_info, &alloc_info, (VkImage *)&image,
                       &allocation, nullptr) != VK_SUCCESS)
        return err::image_create;

    imgv_info.image = image;
    auto [res, vkiv] = dev.createImageView(imgv_info);
    if (res != vk::Result::eSuccess) {
        vmaDestroyImage(allocator, image, allocation);
        return err::image_create;
    }

    imageview = vkiv;
    return err::ok;
}

img::~img() {
    vmaDestroyImage(allocator, image, allocation);
    dev.destroyImageView(imageview);
}
} // namespace vki
