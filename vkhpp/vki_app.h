#pragma once

#include "vki.h"

namespace vki {
struct frm {
    vk::CommandBuffer cmd = nullptr;
    vk::Semaphore img_available = nullptr;
    vk::Semaphore render_done = nullptr;
    vk::Fence can_render = nullptr;
};

struct frmpool {
    vk::CommandPool cmd_pool = nullptr;
    std::vector<frm> frames = {};
    uint8_t idx = 0;

    vk::Device dev;

    frmpool() = default;
    ~frmpool();

    bool create(vk::Device dev, uint32_t queue_idx, uint32_t frm_count);
};

struct app {
    GLFWwindow *window = nullptr;
    vki::base base;
    vki::swp swp;
    frmpool frmpool_;

    uint32_t width = 0;
    uint32_t height = 0;

    app() = default;
    ~app();

    bool create(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug);
    bool create_dev(dev_info info);
    bool create_swp(std::vector<uint32_t> queue_family_indices,
                    vk::SurfaceFormatKHR surface =
                        vk::SurfaceFormatKHR{
                            vk::Format::eB8G8R8A8Srgb,
                            vk::ColorSpaceKHR::eSrgbNonlinear,
                        },
                    vk::PresentModeKHR present = vk::PresentModeKHR::eFifo);
    bool create_frm(uint32_t frame_queue_idx);
};
} // namespace vki
