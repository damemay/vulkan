#include "vki_app.h"
#include <stdexcept>

namespace vki {
frmpool::frmpool(vk::Device dev, uint32_t queue_idx, uint32_t frm_count) {
    auto [res, pool] = dev.createCommandPool({.queueFamilyIndex = queue_idx});
    if (res != vk::Result::eSuccess)
        throw std::runtime_error("Failed to create vk::CommandPool!");

    cmd_pool = pool;

    frames.resize(frm_count);
    for (auto &f : frames) {
        f.cmd = dev.allocateCommandBuffers({.commandPool = cmd_pool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1})
                    .value[0];
        f.img_available = dev.createSemaphore({}).value;
        f.render_done = dev.createSemaphore({}).value;
        f.can_render = dev.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}).value;
    }
}

frmpool::~frmpool() {
    for (auto &f : frames) {
        dev.freeCommandBuffers(cmd_pool, 1, &f.cmd);
        dev.destroySemaphore(f.img_available);
        dev.destroySemaphore(f.render_done);
        dev.destroyFence(f.can_render);
    }
    dev.destroyCommandPool(cmd_pool);
}

app::~app() {
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
}

app::app(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug) {
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW!");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (!(window = glfwCreateWindow(w, h, title, nullptr, nullptr)))
        throw std::runtime_error("Failed to initialize GLFW window!");

    base = std::make_unique<vki::base>(api, debug, window);
}

void app::init_dev(dev_info info) { base->create_device(&info); }

void app::init_swp(std::vector<uint32_t> queue_family_indices, vk::SurfaceFormatKHR surface,
                   vk::PresentModeKHR present) {
    vki::swp_init_info swp_info = {
        .extent = {width, height},
        .surface_format = surface,
        .present_mode = present,
        .queue_family_indices = queue_family_indices,
    };

    swp = std::make_unique<vki::swp>(base->dev, base->pdev, base->surface, swp_info);
}

void app::init_frm(uint32_t frame_queue_idx) {
    frmpool_ = std::make_unique<frmpool>(base->dev, frame_queue_idx, swp->image_count);
    glfwShowWindow(window);
}
} // namespace vki
