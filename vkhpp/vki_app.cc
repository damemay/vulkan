#include "vki_app.h"
#include <stdexcept>

namespace vki {
app::~app() {
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
}

app::app(const char *title, uint32_t w, uint32_t h, uint32_t api, bool debug)
    : alloc(nullptr, vmaDestroyAllocator) {
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW!");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (!(window = glfwCreateWindow(w, h, title, nullptr, nullptr)))
        throw std::runtime_error("Failed to initialize GLFW window!");

    base = vki::base();

    base.create_instance(api, debug);
    base.create_surface(window);
}

void app::init(dev_info info, VmaAllocationCreateFlags vma_flags) {
    base.create_device(&info);
    auto vma = vki::init_vma(vma_flags, base.pdev, base.dev, base.instance);
    alloc.reset(vma);
}

void app::init(std::vector<uint32_t> queue_family_indices, vk::SurfaceFormatKHR surface,
               vk::PresentModeKHR present) {
    vki::swp_init_info swp_info = {
        .extent = {width, height},
        .surface_format = surface,
        .present_mode = present,
        .queue_family_indices = queue_family_indices,
    };

    swp = std::make_unique<vki::swp>(base.dev, base.pdev, base.surface, swp_info);
    glfwShowWindow(window);
}
} // namespace vki
