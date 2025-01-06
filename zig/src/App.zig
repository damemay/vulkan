//! Structure holding and managing application state.

const std = @import("std");

const c = @import("c.zig");
const vk = @import("vk.zig");

const containsBitFlag = @import("viz.zig").containsBitFlag;

const Self = @This();

window: *c.GLFWwindow,
allocator: std.mem.Allocator,
// Vulkan handles
instance: c.VkInstance,
debug_msg: c.VkDebugUtilsMessengerEXT,
surface: c.VkSurfaceKHR,
pdev: c.VkPhysicalDevice,
dev: c.VkDevice,
swapchain: c.VkSwapchainKHR,
vma: c.VmaAllocator,
// Vulkan Swapchain data
present_mode: c.VkPresentModeKHR,
format: c.VkFormat,
color_space: c.VkColorSpaceKHR,
current_extent: c.VkExtent2D,
swapchain_images: []c.VkImage,
swapchain_image_views: []c.VkImageView,
// Vulkan Queues
graphics: vk.Queue,
compute: vk.Queue,
present: vk.Queue,
transfer: vk.Queue,
// App state
debug: bool,
additional_support: bool,

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

pub const InitError = error{
    GlfwInit,
    GlfwWindow,
    VulkanLoad,
    VulkanNoRequiredDevice,
    VulkanNoRequiredQueues,
};

pub const InitOptions = struct {
    title: []const u8 = "viz. vulkan in zig",
    width: u32 = 1280,
    height: u32 = 720,
    debug: bool = false,
    vsync: bool = false,
};

pub fn init(opt: InitOptions, allocator: std.mem.Allocator) !Self {
    var self: Self = undefined;
    self.allocator = allocator;
    self.debug = opt.debug;

    try self.initGlfw(opt);
    errdefer {
        c.glfwDestroyWindow(self.window);
        c.glfwTerminate();
    }

    try self.initInstance();
    errdefer c.vkDestroyInstance.?(self.instance, null);

    if (opt.debug) try self.initDebugMsg();
    errdefer if (opt.debug)
        c.vkDestroyDebugUtilsMessengerEXT.?(self.instance, self.debug_msg, null);

    try self.initSurface();
    errdefer c.vkDestroySurfaceKHR.?(self.instance, self.surface, null);

    try self.initDevice();
    errdefer c.vkDestroyDevice.?(self.dev, null);

    try self.initSwapchain(opt);
    errdefer self.destroySwapchain();

    try self.initVma();
    errdefer c.vmaDestroyAllocator(self.vma);

    return self;
}

pub fn deinit(self: Self) void {
    c.vmaDestroyAllocator(self.vma);
    self.destroySwapchain();
    c.vkDestroyDevice.?(self.dev, null);
    c.vkDestroySurfaceKHR.?(self.instance, self.surface, null);
    if (self.debug) c.vkDestroyDebugUtilsMessengerEXT.?(self.instance, self.debug_msg, null);
    c.vkDestroyInstance.?(self.instance, null);
    c.glfwDestroyWindow(self.window);
    c.glfwTerminate();
}

pub fn createSwapchain(
    self: *Self,
    width: u32,
    height: u32,
    old: c.VkSwapchainKHR,
) !c.VkSwapchainKHR {
    var caps: c.VkSurfaceCapabilitiesKHR = undefined;
    _ = try vk.castResult(c.vkGetPhysicalDeviceSurfaceCapabilitiesKHR.?(
        self.pdev,
        self.surface,
        @ptrCast(&caps),
    ));

    self.current_extent = c.VkExtent2D{
        .width = std.math.clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
        .height = std.math.clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height),
    };

    const image_l = if (caps.minImageCount == caps.maxImageCount)
        caps.maxImageCount
    else
        caps.minImageCount + 1;

    const unique_indices = try self.getUniqueSwapchainQueueIndices();
    defer self.allocator.free(unique_indices);

    const swapchain_info = c.VkSwapchainCreateInfoKHR{
        .sType = c.VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = self.surface,
        .minImageCount = image_l,
        .imageFormat = self.format,
        .imageColorSpace = self.color_space,
        .imageExtent = self.current_extent,
        .imageArrayLayers = 1,
        .imageUsage = c.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | c.VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = if (unique_indices.len > 1)
            c.VK_SHARING_MODE_CONCURRENT
        else
            c.VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = @intCast(unique_indices.len),
        .pQueueFamilyIndices = @ptrCast(unique_indices.ptr),
        .preTransform = caps.currentTransform,
        .compositeAlpha = c.VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = self.present_mode,
        .clipped = c.VK_TRUE,
        .oldSwapchain = old,
    };

    var swapchain: c.VkSwapchainKHR = undefined;
    _ = try vk.castResult(c.vkCreateSwapchainKHR.?(
        self.dev,
        @ptrCast(&swapchain_info),
        null,
        @ptrCast(&swapchain),
    ));

    var image_count: u32 = 0;
    _ = try vk.castResult(c.vkGetSwapchainImagesKHR.?(
        self.dev,
        swapchain,
        @ptrCast(&image_count),
        null,
    ));

    self.swapchain_images = try self.allocator.alloc(c.VkImage, image_count);
    _ = try vk.castResult(c.vkGetSwapchainImagesKHR.?(
        self.dev,
        swapchain,
        @ptrCast(&image_count),
        @ptrCast(self.swapchain_images.ptr),
    ));

    self.swapchain_image_views = try self.allocator.alloc(c.VkImageView, image_count);

    for (self.swapchain_images, self.swapchain_image_views) |img, *imgv| {
        const imgv_info = c.VkImageViewCreateInfo{
            .sType = c.VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = img,
            .viewType = c.VK_IMAGE_VIEW_TYPE_2D,
            .format = self.format,
            .components = c.VkComponentMapping{
                .r = c.VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = c.VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = c.VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = c.VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = c.VkImageSubresourceRange{
                .aspectMask = c.VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        _ = try vk.castResult(c.vkCreateImageView.?(
            self.dev,
            @ptrCast(&imgv_info),
            null,
            @ptrCast(&imgv.*),
        ));
    }

    return swapchain;
}

pub fn destroySwapchain(self: Self) void {
    c.vkDestroySwapchainKHR.?(self.dev, self.swapchain, null);
    for (self.swapchain_image_views) |imgv| c.vkDestroyImageView.?(self.dev, imgv, null);
    self.allocator.free(self.swapchain_images);
    self.allocator.free(self.swapchain_image_views);
}

fn initGlfw(self: *Self, opt: InitOptions) !void {
    if (c.glfwInit() != c.GLFW_TRUE) return error.GlfwInit;

    c.glfwWindowHint(c.GLFW_VISIBLE, c.GLFW_FALSE);
    c.glfwWindowHint(c.GLFW_CLIENT_API, c.GLFW_NO_API);
    self.window = c.glfwCreateWindow(
        @intCast(opt.width),
        @intCast(opt.height),
        @ptrCast(opt.title),
        null,
        null,
    ) orelse return error.GlfwWindow;
}

fn initInstance(self: *Self) !void {
    if (try vk.castResult(c.volkInitialize()) != .success) return error.VulkanLoad;

    var glfw_ext_l: u32 = 0;
    const glfw_ext = c.glfwGetRequiredInstanceExtensions(@ptrCast(&glfw_ext_l));

    const extension_l = if (self.debug) glfw_ext_l + 1 else glfw_ext_l;

    const extensions = try self.allocator.alloc([*:0]const u8, extension_l);
    defer self.allocator.free(extensions);

    for (0..glfw_ext_l) |i| extensions[i] = glfw_ext[i];
    if (self.debug) extensions[glfw_ext_l] = c.VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

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
        @ptrCast(&self.instance),
    ));

    c.volkLoadInstanceOnly(self.instance);
}

fn initDebugMsg(self: *Self) !void {
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
        self.instance,
        @ptrCast(&debug_info),
        null,
        @ptrCast(&self.debug_msg),
    ));
}

fn initSurface(self: *Self) !void {
    _ = try vk.castResult(c.glfwCreateWindowSurface(
        self.instance,
        self.window,
        null,
        @ptrCast(&self.surface),
    ));
}

fn initDevice(self: *Self) !void {
    try self.pickDevice();
    try self.pickQueues();

    const queue_priority: f32 = 1.0;
    const unique_indices = try self.getUniqueQueueIndices();
    defer self.allocator.free(unique_indices);

    const queue_infos = try self.allocator.alloc(c.VkDeviceQueueCreateInfo, unique_indices.len);
    defer self.allocator.free(queue_infos);

    for (queue_infos, unique_indices) |*info, i| {
        info.* = c.VkDeviceQueueCreateInfo{
            .sType = c.VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = i,
            .queueCount = 1,
            .pQueuePriorities = @ptrCast(&queue_priority),
        };
    }

    const extensions = if (self.additional_support) blk: {
        var tmp = try self.allocator.alloc(
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
    } else try self.allocator.dupe([*:0]const u8, required_device_extensions);
    defer self.allocator.free(extensions);

    const device_info = c.VkDeviceCreateInfo{
        .sType = c.VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = @ptrCast(@constCast(&vulkan_features2)),
        .enabledExtensionCount = @intCast(extensions.len),
        .ppEnabledExtensionNames = @ptrCast(extensions.ptr),
        .queueCreateInfoCount = @intCast(queue_infos.len),
        .pQueueCreateInfos = @ptrCast(queue_infos.ptr),
    };

    _ = try vk.castResult(c.vkCreateDevice.?(
        self.pdev,
        @ptrCast(&device_info),
        null,
        @ptrCast(&self.dev),
    ));

    c.volkLoadDevice(self.dev);

    c.vkGetDeviceQueue.?(self.dev, self.graphics.index, 0, @ptrCast(&self.graphics.handle));
    c.vkGetDeviceQueue.?(self.dev, self.compute.index, 0, @ptrCast(&self.compute.handle));
    c.vkGetDeviceQueue.?(self.dev, self.present.index, 0, @ptrCast(&self.present.handle));
    c.vkGetDeviceQueue.?(self.dev, self.transfer.index, 0, @ptrCast(&self.transfer.handle));
}

fn initSwapchain(self: *Self, opt: InitOptions) !void {
    self.format = format;
    self.color_space = color_space;
    self.present_mode = blk: {
        if (opt.vsync) break :blk c.VK_PRESENT_MODE_FIFO_KHR;

        var mode_l: u32 = 0;
        _ = try vk.castResult(c.vkGetPhysicalDeviceSurfacePresentModesKHR.?(
            self.pdev,
            self.surface,
            @ptrCast(&mode_l),
            null,
        ));

        const modes = try self.allocator.alloc(c.VkPresentModeKHR, mode_l);
        defer self.allocator.free(modes);

        for (modes) |mode| {
            if (mode == c.VK_PRESENT_MODE_IMMEDIATE_KHR)
                break :blk c.VK_PRESENT_MODE_IMMEDIATE_KHR;
        } else {
            break :blk c.VK_PRESENT_MODE_FIFO_KHR;
        }
    };

    self.swapchain = try self.createSwapchain(opt.width, opt.height, null);
}

fn initVma(self: *Self) !void {
    const functions = c.VmaVulkanFunctions{
        .vkGetInstanceProcAddr = c.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = c.vkGetDeviceProcAddr,
    };

    const vma_info = c.VmaAllocatorCreateInfo{
        .flags = c.VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = self.pdev,
        .device = self.dev,
        .instance = self.instance,
        .pVulkanFunctions = @ptrCast(&functions),
    };

    _ = try vk.castResult(c.vmaCreateAllocator(@ptrCast(&vma_info), @ptrCast(&self.vma)));
}

fn pickDevice(self: *Self) !void {
    var device_l: u32 = 0;
    _ = try vk.castResult(c.vkEnumeratePhysicalDevices.?(
        self.instance,
        @ptrCast(&device_l),
        null,
    ));

    const devices = try self.allocator.alloc(c.VkPhysicalDevice, device_l);
    defer self.allocator.free(devices);

    _ = try vk.castResult(c.vkEnumeratePhysicalDevices.?(
        self.instance,
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

        const extensions = try self.allocator.alloc(c.VkExtensionProperties, extension_l);
        defer self.allocator.free(extensions);

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
        self.pdev = best;
        self.additional_support = true;
    } else if (weak_pick) |weak| {
        self.pdev = weak;
        self.additional_support = false;
    } else {
        return error.VulkanNoRequiredDevice;
    }
}

fn pickQueues(self: *Self) !void {
    var queue_l: u32 = 0;
    c.vkGetPhysicalDeviceQueueFamilyProperties.?(self.pdev, @ptrCast(&queue_l), null);

    const properties = try self.allocator.alloc(c.VkQueueFamilyProperties, queue_l);
    defer self.allocator.free(properties);

    c.vkGetPhysicalDeviceQueueFamilyProperties.?(
        self.pdev,
        @ptrCast(&queue_l),
        @ptrCast(properties.ptr),
    );

    var graphics: ?vk.Queue = null;
    var compute: ?vk.Queue = null;
    var present: ?vk.Queue = null;
    var transfer: ?vk.Queue = null;

    for (properties, 0..) |prop, i| {
        const index: u32 = @intCast(i);
        if (graphics == null and containsBitFlag(prop.queueFlags, c.VK_QUEUE_GRAPHICS_BIT)) {
            graphics = .{ .index = index };
        } else if (compute == null and containsBitFlag(prop.queueFlags, c.VK_QUEUE_COMPUTE_BIT)) {
            compute = .{ .index = index };
        } else if (transfer == null and containsBitFlag(prop.queueFlags, c.VK_QUEUE_TRANSFER_BIT)) {
            transfer = .{ .index = index };
        }

        if (present == null and try canQueuePresent(self.pdev, self.surface, index))
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

    self.graphics = graphics.?;
    self.compute = compute.?;
    self.present = present.?;
    self.transfer = transfer.?;
}

fn getUniqueSwapchainQueueIndices(self: Self) ![]u32 {
    const all = [_]u32{
        self.graphics.index,
        self.present.index,
    };
    var unique = try std.ArrayList(u32).initCapacity(self.allocator, 2);

    for (all) |i| {
        for (unique.items) |ui| {
            if (i == ui) break;
        } else {
            try unique.append(i);
        }
    }

    return try unique.toOwnedSlice();
}

fn getUniqueQueueIndices(self: *Self) ![]u32 {
    const all = [_]u32{
        self.graphics.index,
        self.compute.index,
        self.present.index,
        self.transfer.index,
    };
    var unique = try std.ArrayList(u32).initCapacity(self.allocator, 4);

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
        if (containsBitFlag(prop.queueFlags, flag)) return index;
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
