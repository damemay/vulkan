#include "vki.h"
#include "vki_app.h"
#include <GLFW/glfw3.h>

constexpr uint32_t width = 1280;
constexpr uint32_t height = 720;

int main() {
    auto app = vki::app();
    vki::dev_info base_info = {
        .device_queues = {{vk::QueueFlagBits::eGraphics}},
        .device_extensions = {vk::KHRSwapchainExtensionName},
    };
    assert(app.init("test", width, height, vk::ApiVersion10, true) == vki::err::ok);
    assert(app.init(base_info) == vki::err::ok);
    assert(app.init({base_info.device_queues[0].index}) == vki::err::ok);
}
