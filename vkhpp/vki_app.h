#pragma once

#include "vki.h"
#include <memory>

namespace vki {
struct app {
    GLFWwindow *window = nullptr;
    vki::base base;
    std::unique_ptr<vki::swp> swp;
    std::unique_ptr<VmaAllocator_T, decltype(&vmaDestroyAllocator)> alloc;

    uint32_t width = 0;
    uint32_t height = 0;

    app(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug);
    ~app();

    void init(dev_info info, VmaAllocationCreateFlags vma_flags);
    void
    init(std::vector<uint32_t> queue_family_indices,
         vk::SurfaceFormatKHR surface = vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb,
                                                             vk::ColorSpaceKHR::eSrgbNonlinear},
         vk::PresentModeKHR present = vk::PresentModeKHR::eFifo);
};
} // namespace vki
