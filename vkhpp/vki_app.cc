#include "vki_app.h"

namespace vki {
app::~app() {
    if (alloc)
        vmaDestroyAllocator(alloc);
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
}

err app::init(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug) {
    if (!glfwInit())
        return err::glfw;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (!(window = glfwCreateWindow(w, h, title, nullptr, nullptr)))
        return err::glfw;

    base = vki::base();

    if (err res = base.create_instance(api, debug); res != err::ok)
        return res;

    if (err res = base.create_surface(window); res != err::ok)
        return res;

    return err::ok;
}

err app::init(dev_info info) {
    if (err res = base.create_device(&info); res != err::ok)
        return res;

    if (!(alloc = vki::init_vma(0, base.pdev, base.dev, base.instance)))
        return err::vma;

    return err::ok;
}

err app::init(std::vector<uint32_t> queue_family_indices, vk::SurfaceFormatKHR surface,
              vk::PresentModeKHR present) {
    vki::swp_init_info swp_info = {
        .extent = {width, height},
        .surface_format = surface,
        .present_mode = present,
        .queue_family_indices = queue_family_indices,
    };

    swp = vki::swp();
    if (err res = swp.create(base.dev, base.pdev, base.surface, swp_info); res != err::ok)
        return res;

    glfwShowWindow(window);

    return err::ok;
}
} // namespace vki
