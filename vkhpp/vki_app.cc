#include "vki_app.h"
#include <stdexcept>

namespace vki {
bool frmpool::create(vk::Device dev, uint32_t queue_idx, uint32_t frm_count) {
    auto [res, pool] = dev.createCommandPool({.queueFamilyIndex = queue_idx});
    if (res != vk::Result::eSuccess)
        return false;

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

    return true;
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

bool app::create(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug) {
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (!(window = glfwCreateWindow(w, h, title, nullptr, nullptr)))
        return false;

    return base.create(api, debug, window);
}

bool app::create_dev(dev_info info) { return base.create_device(&info); }

bool app::create_swp(std::vector<uint32_t> queue_family_indices, vk::SurfaceFormatKHR surface,
                     vk::PresentModeKHR present) {
    vki::swp_init_info swp_info = {
        .extent = {width, height},
        .surface_format = surface,
        .present_mode = present,
        .queue_family_indices = queue_family_indices,
    };

    return swp.create(base.dev, base.pdev, base.surface, swp_info);
}

bool app::create_frm(uint32_t frame_queue_idx) {
    if (!frmpool_.create(base.dev, frame_queue_idx, swp.image_count))
        return false;
    glfwShowWindow(window);
    return true;
}
} // namespace vki
