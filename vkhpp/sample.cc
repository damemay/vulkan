#include "vki.h"
#include "vki_app.h"
#include <GLFW/glfw3.h>

constexpr const char *title_ = "sample";
constexpr uint32_t width_ = 1280;
constexpr uint32_t height_ = 720;
constexpr uint32_t api_ = vk::ApiVersion10;
constexpr bool debug_ = true;

struct sample_app : public vki::app {
    vki::queue_info gfx;
    vki::gfxp pipeline;

    ~sample_app() {};
    sample_app() {
        assert(app::create(title_, width_, height_, api_, debug_));
        vki::dev_info dev_info = {
            .device_queues = {{vk::QueueFlagBits::eGraphics}},
            .device_extensions = {vk::KHRSwapchainExtensionName},
        };
        assert(app::create_dev(dev_info));

        gfx = dev_info.device_queues[0];
        assert(app::create_swp({gfx.index}));
        assert(app::create_frm(gfx.index));

        run();
    }

    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }
};

int main() { auto app = sample_app(); }
