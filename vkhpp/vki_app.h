#pragma once

#include "vki.h"

namespace vki {
struct app {
    GLFWwindow *window = nullptr;
    VmaAllocator alloc = nullptr;
    vki::base base;
    vki::swp swp;

    uint32_t width = 0;
    uint32_t height = 0;

    app() = default;
    ~app();

    err init(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug);
    err init(dev_info info);
    err init(std::vector<uint32_t> queue_family_indices,
             vk::SurfaceFormatKHR surface = vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb,
                                                                 vk::ColorSpaceKHR::eSrgbNonlinear},
             vk::PresentModeKHR present = vk::PresentModeKHR::eFifo);
};
} // namespace vki
