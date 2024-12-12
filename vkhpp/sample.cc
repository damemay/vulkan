#include "vki.h"
#include "vki_app.h"
#include <GLFW/glfw3.h>

constexpr uint32_t width = 1280;
constexpr uint32_t height = 720;

int main() {
    auto app = vki::app("test", width, height, vk::ApiVersion10, true);
    vki::dev_info dev_info = {
        .device_queues = {{vk::QueueFlagBits::eGraphics}},
        .device_extensions = {vk::KHRSwapchainExtensionName},
    };
    app.init(dev_info, 0);
    app.init({dev_info.device_queues[0].index});
}
