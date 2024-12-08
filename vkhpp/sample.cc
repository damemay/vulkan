#include "vki.h"
#include <GLFW/glfw3.h>

int main() {
    assert(glfwInit());
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window = glfwCreateWindow(800, 600, "", nullptr, nullptr);
    assert(window);

    vki::dev_info base_info = {
        .device_queues = {{vk::QueueFlagBits::eGraphics}},
        .device_extensions = {vk::KHRSwapchainExtensionName},
    };

    auto base = vki::base();
    assert(base.create_instance(vk::ApiVersion10, true));
    assert(base.create_surface(window));
    assert(base.create_device(&base_info));

    auto alloc = vki::init_vma(0, base.pdev, base.dev, base.instance);
    assert(alloc);

    vki::swp_init_info swp_info = {
        .extent = {800, 600},
        .queue_family_indices = {base_info.device_queues[0].index},
    };

    auto swp = vki::swp(swp_info, base.dev);
    assert(swp.create(base.pdev, base.surface));

    vk::ImageCreateInfo img_info = {
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {swp.extent.width, swp.extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage |
                 vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
    };
    vk::ImageViewCreateInfo ivi = {
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .levelCount = 1,
                             .layerCount = 1},
    };
    VmaAllocationCreateInfo ai = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VkMemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal),
    };

    auto img = new vki::img();
    assert(img->create(img_info, ivi, ai, base.dev, alloc));
    delete img;

    vmaDestroyAllocator(alloc);
    glfwDestroyWindow(window);
    glfwTerminate();
}
