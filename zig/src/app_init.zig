const std = @import("std");

const c = @import("c.zig");
const vk = @import("vk.zig");
const viz = @import("viz.zig");

const App = viz.App;
const Queue = viz.Queue;

pub const Error = error{
    GlfwInit,
    GlfwWindow,
    VulkanLoad,
    VulkanNoRequiredDevice,
    VulkanNoRequiredQueues,
};

pub const Options = struct {
    title: []const u8 = "viz. vulkan in zig",
    width: u32 = 1280,
    height: u32 = 720,
    debug: bool = false,
    vsync: bool = false,
};

const api_version = c.VK_API_VERSION_1_2;
const format: c.VkFormat = c.VK_FORMAT_B8G8R8A8_SRGB;
const color_space: c.VkColorSpaceKHR = c.VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
const required_device_extensions = &[_][*:0]const u8{
    c.VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    c.VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
};
const additional_device_extensions = &[_][*:0]const u8{
    c.VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    c.VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    c.VK_KHR_RAY_QUERY_EXTENSION_NAME,
    c.VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    c.VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    c.VK_EXT_MESH_SHADER_EXTENSION_NAME,
};
const acceleration_structure_features = c.VkPhysicalDeviceAccelerationStructureFeaturesKHR{
    .sType = c.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
    .accelerationStructure = c.VK_TRUE,
};
const ray_tracing_pipeline_features = c.VkPhysicalDeviceRayTracingPipelineFeaturesKHR{
    .sType = c.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
    .pNext = @ptrCast(@constCast(&acceleration_structure_features)),
    .rayTracingPipeline = c.VK_TRUE,
};
const ray_query_features = c.VkPhysicalDeviceRayQueryFeaturesKHR{
    .sType = c.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    .pNext = @ptrCast(@constCast(&ray_tracing_pipeline_features)),
    .rayQuery = c.VK_TRUE,
};
const mesh_shader_features = c.VkPhysicalDeviceMeshShaderFeaturesEXT{
    .sType = c.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
    .pNext = @ptrCast(@constCast(&ray_query_features)),
    .taskShader = c.VK_TRUE,
    .meshShader = c.VK_TRUE,
};
const vulkan12_features = c.VkPhysicalDeviceVulkan12Features{
    .sType = c.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .pNext = @ptrCast(@constCast(&mesh_shader_features)),
    .imagelessFramebuffer = c.VK_TRUE,
    .descriptorIndexing = c.VK_TRUE,
    .bufferDeviceAddress = c.VK_TRUE,
};
const vulkan_features2 = c.VkPhysicalDeviceFeatures2{
    .sType = c.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext = @ptrCast(@constCast(&vulkan12_features)),
    .features = c.VkPhysicalDeviceFeatures{ .samplerAnisotropy = c.VK_TRUE },
};

pub fn glfw(app: *App, opt: Options) !void {
    if (c.glfwInit() != c.GLFW_TRUE) return error.GlfwInit;

    c.glfwWindowHint(c.GLFW_VISIBLE, c.GLFW_FALSE);
    c.glfwWindowHint(c.GLFW_CLIENT_API, c.GLFW_NO_API);
    app.window = c.glfwCreateWindow(
        @intCast(opt.width),
        @intCast(opt.height),
        @ptrCast(opt.title),
        null,
        null,
    ) orelse return error.GlfwWindow;
}

pub fn instance(app: *App) !void {
    if (try vk.castResult(c.volkInitialize()) != .success) return error.VulkanLoad;

    var glfw_ext_l: u32 = 0;
    const glfw_ext = c.glfwGetRequiredInstanceExtensions(@ptrCast(&glfw_ext_l));

    const extension_l = if (app.debug) glfw_ext_l + 1 else glfw_ext_l;

    const extensions = try app.allocator.alloc([*:0]const u8, extension_l);
    defer app.allocator.free(extensions);

    for (0..glfw_ext_l) |i| extensions[i] = glfw_ext[i];
    if (app.debug) extensions[glfw_ext_l] = c.VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    const instance_info = c.VkInstanceCreateInfo{
        .sType = c.VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = @ptrCast(&c.VkApplicationInfo{
            .sType = c.VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = api_version,
        }),
        .enabledExtensionCount = @intCast(extensions.len),
        .ppEnabledExtensionNames = @ptrCast(extensions.ptr),
    };

    _ = try vk.castResult(c.vkCreateInstance.?(
        @ptrCast(&instance_info),
        null,
        @ptrCast(&app.instance),
    ));

    c.volkLoadInstanceOnly(app.instance);
}

pub fn debugMsg(app: *App) !void {
    const debug_info = c.VkDebugUtilsMessengerCreateInfoEXT{
        .sType = c.VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = c.VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            c.VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            c.VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            c.VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = c.VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            c.VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            c.VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
            c.VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
        .pfnUserCallback = @ptrCast(&debugCallback),
    };

    _ = try vk.castResult(c.vkCreateDebugUtilsMessengerEXT.?(
        app.instance,
        @ptrCast(&debug_info),
        null,
        @ptrCast(&app.debug_msg),
    ));
}

pub fn surface(app: *App) !void {
    _ = try vk.castResult(c.glfwCreateWindowSurface(
        app.instance,
        app.window,
        null,
        @ptrCast(&app.surface),
    ));
}

pub fn device(app: *App) !void {
    try pickDevice(app);
    try pickQueues(app);

    const queue_priority: f32 = 1.0;
    const unique_indices = try getUniqueQueueIndices(app);
    defer app.allocator.free(unique_indices);

    const queue_infos = try app.allocator.alloc(c.VkDeviceQueueCreateInfo, unique_indices.len);
    defer app.allocator.free(queue_infos);

    for (queue_infos, unique_indices) |*info, i| {
        info.* = c.VkDeviceQueueCreateInfo{
            .sType = c.VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = i,
            .queueCount = 1,
            .pQueuePriorities = @ptrCast(&queue_priority),
        };
    }

    const extensions = if (app.additional_support) blk: {
        var tmp = try app.allocator.alloc(
            [*:0]const u8,
            required_device_extensions.len +
                additional_device_extensions.len,
        );
        std.mem.copyForwards(
            [*:0]const u8,
            tmp[0..],
            required_device_extensions,
        );
        std.mem.copyForwards(
            [*:0]const u8,
            tmp[required_device_extensions.len..],
            additional_device_extensions,
        );
        break :blk tmp;
    } else try app.allocator.dupe([*:0]const u8, required_device_extensions);
    defer app.allocator.free(extensions);

    const device_info = c.VkDeviceCreateInfo{
        .sType = c.VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = @ptrCast(@constCast(&vulkan_features2)),
        .enabledExtensionCount = @intCast(extensions.len),
        .ppEnabledExtensionNames = @ptrCast(extensions.ptr),
        .queueCreateInfoCount = @intCast(queue_infos.len),
        .pQueueCreateInfos = @ptrCast(queue_infos.ptr),
    };

    _ = try vk.castResult(c.vkCreateDevice.?(
        app.pdev,
        @ptrCast(&device_info),
        null,
        @ptrCast(&app.dev),
    ));

    c.volkLoadDevice(app.dev);

    c.vkGetDeviceQueue.?(app.dev, app.graphics.index, 0, @ptrCast(&app.graphics.handle));
    c.vkGetDeviceQueue.?(app.dev, app.compute.index, 0, @ptrCast(&app.compute.handle));
    c.vkGetDeviceQueue.?(app.dev, app.present.index, 0, @ptrCast(&app.present.handle));
    c.vkGetDeviceQueue.?(app.dev, app.transfer.index, 0, @ptrCast(&app.transfer.handle));
}

pub fn swapchain(app: *App, opt: Options) !void {
    app.format = format;
    app.color_space = color_space;
    app.present_mode = blk: {
        if (app.vsync) break :blk c.VK_PRESENT_MODE_FIFO_KHR;

        var mode_l: u32 = 0;
        _ = try vk.castResult(c.vkGetPhysicalDeviceSurfacePresentModesKHR.?(
            app.pdev,
            app.surface,
            @ptrCast(&mode_l),
            null,
        ));

        const modes = try app.allocator.alloc(c.VkPresentModeKHR, mode_l);
        defer app.allocator.free(modes);

        for (modes) |mode| {
            if (mode == c.VK_PRESENT_MODE_IMMEDIATE_KHR)
                break :blk c.VK_PRESENT_MODE_IMMEDIATE_KHR;
        } else {
            break :blk c.VK_PRESENT_MODE_FIFO_KHR;
        }
    };

    app.swapchain = try app.createSwapchain(opt.width, opt.height, null);
}

fn pickDevice(app: *App) !void {
    var device_l: u32 = 0;
    _ = try vk.castResult(c.vkEnumeratePhysicalDevices.?(
        app.instance,
        @ptrCast(&device_l),
        null,
    ));

    const devices = try app.allocator.alloc(c.VkPhysicalDevice, device_l);
    defer app.allocator.free(devices);

    _ = try vk.castResult(c.vkEnumeratePhysicalDevices.?(
        app.instance,
        @ptrCast(&device_l),
        @ptrCast(devices.ptr),
    ));

    var weak_pick: ?c.VkPhysicalDevice = null;
    var best_pick: ?c.VkPhysicalDevice = null;
    for (devices) |pdevice| {
        var extension_l: u32 = 0;
        _ = try vk.castResult(c.vkEnumerateDeviceExtensionProperties.?(
            pdevice,
            null,
            @ptrCast(&extension_l),
            null,
        ));

        const extensions = try app.allocator.alloc(c.VkExtensionProperties, extension_l);
        defer app.allocator.free(extensions);

        _ = try vk.castResult(c.vkEnumerateDeviceExtensionProperties.?(
            pdevice,
            null,
            @ptrCast(&extension_l),
            @ptrCast(extensions.ptr),
        ));

        var found_required_extensions: u32 = 0;
        var found_additional_extensions: u32 = 0;
        for (extensions) |extension| {
            for (required_device_extensions) |required_extension| {
                if (std.mem.orderZ(
                    u8,
                    @ptrCast(&extension.extensionName),
                    required_extension,
                ) == .eq)
                    found_required_extensions += 1;
            }
            for (additional_device_extensions) |additional_extension| {
                if (std.mem.orderZ(
                    u8,
                    @ptrCast(&extension.extensionName),
                    additional_extension,
                ) == .eq)
                    found_additional_extensions += 1;
            }
        }

        if (found_required_extensions == required_device_extensions.len and
            found_additional_extensions != additional_device_extensions.len)
        {
            weak_pick = pdevice;
        } else if (found_required_extensions == required_device_extensions.len and
            found_additional_extensions == additional_device_extensions.len)
        {
            best_pick = pdevice;
        }
    }

    if (best_pick) |best| {
        app.pdev = best;
        app.additional_support = true;
    } else if (weak_pick) |weak| {
        app.pdev = weak;
        app.additional_support = false;
    } else {
        return error.VulkanNoRequiredDevice;
    }
}

fn pickQueues(app: *App) !void {
    var queue_l: u32 = 0;
    c.vkGetPhysicalDeviceQueueFamilyProperties.?(app.pdev, @ptrCast(&queue_l), null);

    const properties = try app.allocator.alloc(c.VkQueueFamilyProperties, queue_l);
    defer app.allocator.free(properties);

    c.vkGetPhysicalDeviceQueueFamilyProperties.?(
        app.pdev,
        @ptrCast(&queue_l),
        @ptrCast(properties.ptr),
    );

    var graphics: ?Queue = null;
    var compute: ?Queue = null;
    var present: ?Queue = null;
    var transfer: ?Queue = null;

    for (properties, 0..) |prop, i| {
        const index: u32 = @intCast(i);
        if (graphics == null and viz.containsBitFlag(prop.queueFlags, c.VK_QUEUE_GRAPHICS_BIT)) {
            graphics = .{ .index = index };
        } else if (compute == null and viz.containsBitFlag(prop.queueFlags, c.VK_QUEUE_COMPUTE_BIT)) {
            compute = .{ .index = index };
        } else if (transfer == null and viz.containsBitFlag(prop.queueFlags, c.VK_QUEUE_TRANSFER_BIT)) {
            transfer = .{ .index = index };
        }

        if (present == null and try canQueuePresent(app.pdev, app.surface, index))
            present = .{ .index = index };
        if (graphics != null and compute != null and present != null and transfer != null)
            break;
    }

    // Perform checks and search for fallback non-independent queues
    if (graphics == null and present == null) return error.VulkanNoRequiredQueues;
    if (compute == null) {
        if (findQueueFlag(properties, c.VK_QUEUE_COMPUTE_BIT)) |index| {
            compute = .{ .index = index };
        } else {
            return error.VulkanNoRequiredQueues;
        }
    }
    if (transfer == null) {
        if (findQueueFlag(properties, c.VK_QUEUE_TRANSFER_BIT)) |index| {
            transfer = .{ .index = index };
        } else {
            return error.VulkanNoRequiredQueues;
        }
    }

    app.graphics = graphics.?;
    app.compute = compute.?;
    app.present = present.?;
    app.transfer = transfer.?;
}

fn getUniqueQueueIndices(app: *App) ![]u32 {
    const all = [_]u32{
        app.graphics.index,
        app.compute.index,
        app.present.index,
        app.transfer.index,
    };
    var unique = try std.ArrayList(u32).initCapacity(app.allocator, 4);

    for (all) |i| {
        for (unique.items) |ui| {
            if (i == ui) break;
        } else {
            try unique.append(i);
        }
    }

    return try unique.toOwnedSlice();
}

fn canQueuePresent(pdev: c.VkPhysicalDevice, _surface: c.VkSurfaceKHR, index: u32) !bool {
    var can_present = c.VK_FALSE;

    _ = try vk.castResult(c.vkGetPhysicalDeviceSurfaceSupportKHR.?(
        pdev,
        index,
        _surface,
        @ptrCast(&can_present),
    ));

    if (can_present == c.VK_TRUE) return true;
    return false;
}

fn findQueueFlag(properties: []c.VkQueueFamilyProperties, flag: c.VkQueueFlagBits) ?u32 {
    for (properties, 0..) |prop, i| {
        const index: u32 = @intCast(i);
        if (viz.containsBitFlag(prop.queueFlags, flag)) return index;
    }
    return null;
}

fn debugCallback(
    _: c.VkDebugUtilsMessageSeverityFlagBitsEXT,
    _: c.VkDebugUtilsMessageTypeFlagsEXT,
    data: *c.VkDebugUtilsMessengerCallbackDataEXT,
    _: ?*anyopaque,
) callconv(.C) c.VkBool32 {
    std.debug.print("{s}\n", .{data.pMessage});
    return c.VK_FALSE;
}
