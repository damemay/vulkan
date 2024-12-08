# vulkan boilerstrap

Vulkan boilerplate/bootstrap with SDL3 window tailored for my own usage written in C++20 standard with somewhat C-ish style.

It's not a one stop solution. It was made more to learn Vulkan API and it's possibilities without limiting myself to creating one kind of a rendering engine.
Nevertheless, I tried to make it as modular as possible, so I don't need to recode initialization boilerplate.

## Usage

Helper objects more or less are structured as below:

```cpp
Object object {&context};
object.create(...);
assert(object.all_valid());
object.clean();
```

`vb::Context` is the main structure, that is initialized by multiple functions, accepting info structures akin to Vulkan handles creation. It's the only struct with defined destructor without `clean()` method.

You can follow the example below:

```cpp
auto vbc = vb::Context();

assert(vbc.init());

vb::ContextInstanceWindowInfo window_info = {};
assert(vbc.create_instance_window(window_info));

vb::ContextDeviceInfo device_info = {};
assert(vbc.create_device(device_info));
// VK_KHR_SWAPCHAIN is always requested, even when it's not declared anywhere!

vb::ContextSwapchainInfo swapchain_info = {};
assert(vbc.create_surface_swapchain(swapchain_info));

assert(vbc.init_vma());

auto graphics_queue = vbc.find_queue(vb::Queue::Graphics);
assert(graphics_queue);

auto cmdpool = vb::CommandPool(&vbc);
cmdpool.create(graphics_queue->index);
assert(cmdpool.all_valid());

auto cmdbf = cmdpool.allocate();

assert(vbc.init_command_submitter(cmdbf, graphics_queue->queue, graphics_queue->index));

// ...

cmdpool.clean();
```

Check out the [vb.h](vb/vb.h) header for documentation.

You can find basic sample Vulkan apps inside [samples/](samples).

## Building

Make sure to clone the repository with all submodules.

VB is built with CMake and it's trivial to include it in your own CMake project:

```cmake
add_subdirectory(vb)
target_link_libraries(${PROJECT_NAME}
    vb::vb
    SDL3::SDL3
    m
)
```

For samples, set `VB_SAMPLE` to `ON` when building.
