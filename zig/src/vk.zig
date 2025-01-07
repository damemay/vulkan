//! This file provides various Vulkan wrappers and helpers.
const std = @import("std");
const c = @import("c.zig");
const InitOptions = @import("App.zig").InitOptions;

pub var allocator: std.mem.Allocator = undefined;
// Vulkan handles:
pub var instance: c.VkInstance = undefined;
pub var debug_msg: c.VkDebugUtilsMessengerEXT = undefined;
pub var physical_device: c.VkPhysicalDevice = undefined;
pub var device: c.VkDevice = undefined;
pub var surface: c.VkSurfaceKHR = undefined;
pub var swapchain: Swapchain = undefined;
pub var vma_allocator: c.VmaAllocator = undefined;
// Vulkan Queues:
pub var graphics: Queue = undefined;
pub var compute: Queue = undefined;
pub var present: Queue = undefined;
pub var transfer: Queue = undefined;
// State:
pub var debug: bool = undefined;
pub var additional_support: bool = undefined;

pub fn init(opt: InitOptions, window: *c.GLFWwindow, alloca: std.mem.Allocator) !void {
    allocator = alloca;
    debug = opt.debug;

    try initInstance();
    errdefer c.vkDestroyInstance.?(instance, null);

    if (opt.debug) try initDebugMsg();
    errdefer if (opt.debug)
        c.vkDestroyDebugUtilsMessengerEXT.?(instance, debug_msg, null);

    try initSurface(window);
    errdefer c.vkDestroySurfaceKHR.?(instance, surface, null);

    try initDevice();
    errdefer c.vkDestroyDevice.?(device, null);

    swapchain = try Swapchain.init(opt);
    errdefer swapchain.destroy();

    try initVma();
    errdefer c.vmaDestroyAllocator(vma_allocator);
}

pub fn deinit() void {
    c.vmaDestroyAllocator(vma_allocator);
    swapchain.destroy();
    c.vkDestroyDevice.?(device, null);
    c.vkDestroySurfaceKHR.?(instance, surface, null);
    if (debug) c.vkDestroyDebugUtilsMessengerEXT.?(instance, debug_msg, null);
    c.vkDestroyInstance.?(instance, null);
}

/// Queue wrapper providing queue family index and VkQueue handle.
pub const Queue = struct {
    index: u32,
    handle: c.VkQueue = null,
};

/// Swapchain wrapper
pub const Swapchain = struct {
    present_mode: c.VkPresentModeKHR,
    format: c.VkFormat,
    color_space: c.VkColorSpaceKHR,
    current_extent: c.VkExtent2D,
    swapchain_images: []c.VkImage,
    swapchain_image_views: []c.VkImageView,
    handle: c.VkSwapchainKHR,

    const Self = @This();

    fn init(opt: InitOptions) !Self {
        var self: Self = undefined;
        self.format = format;
        self.color_space = color_space;
        self.present_mode = blk: {
            if (opt.vsync) break :blk c.VK_PRESENT_MODE_FIFO_KHR;

            var mode_l: u32 = 0;
            _ = try castResult(c.vkGetPhysicalDeviceSurfacePresentModesKHR.?(
                physical_device,
                surface,
                @ptrCast(&mode_l),
                null,
            ));

            const modes = try allocator.alloc(c.VkPresentModeKHR, mode_l);
            defer allocator.free(modes);

            for (modes) |mode| {
                if (mode == c.VK_PRESENT_MODE_IMMEDIATE_KHR)
                    break :blk c.VK_PRESENT_MODE_IMMEDIATE_KHR;
            } else {
                break :blk c.VK_PRESENT_MODE_FIFO_KHR;
            }
        };

        self.handle = try self.create(opt.width, opt.height, null);
        return self;
    }

    pub fn create(self: *Self, width: u32, height: u32, old: c.VkSwapchainKHR) !c.VkSwapchainKHR {
        var caps: c.VkSurfaceCapabilitiesKHR = undefined;
        _ = try castResult(c.vkGetPhysicalDeviceSurfaceCapabilitiesKHR.?(
            physical_device,
            surface,
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

        const unique_indices = try getUniqueSwapchainQueueIndices();
        defer allocator.free(unique_indices);

        const swapchain_info = c.VkSwapchainCreateInfoKHR{
            .sType = c.VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
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

        var _swapchain: c.VkSwapchainKHR = undefined;
        _ = try castResult(c.vkCreateSwapchainKHR.?(
            device,
            @ptrCast(&swapchain_info),
            null,
            @ptrCast(&_swapchain),
        ));

        var image_count: u32 = 0;
        _ = try castResult(c.vkGetSwapchainImagesKHR.?(
            device,
            _swapchain,
            @ptrCast(&image_count),
            null,
        ));

        self.swapchain_images = try allocator.alloc(c.VkImage, image_count);
        _ = try castResult(c.vkGetSwapchainImagesKHR.?(
            device,
            _swapchain,
            @ptrCast(&image_count),
            @ptrCast(self.swapchain_images.ptr),
        ));

        self.swapchain_image_views = try allocator.alloc(c.VkImageView, image_count);

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

            _ = try castResult(c.vkCreateImageView.?(
                device,
                @ptrCast(&imgv_info),
                null,
                @ptrCast(&imgv.*),
            ));
        }

        return _swapchain;
    }

    pub fn destroy(self: Self) void {
        c.vkDestroySwapchainKHR.?(device, self.handle, null);
        for (self.swapchain_image_views) |imgv| c.vkDestroyImageView.?(device, imgv, null);
        allocator.free(self.swapchain_images);
        allocator.free(self.swapchain_image_views);
    }

    fn getUniqueSwapchainQueueIndices() ![]u32 {
        const all = [_]u32{
            graphics.index,
            present.index,
        };
        var unique = try std.ArrayList(u32).initCapacity(allocator, 2);

        for (all) |i| {
            for (unique.items) |ui| {
                if (i == ui) break;
            } else {
                try unique.append(i);
            }
        }

        return try unique.toOwnedSlice();
    }
};

/// Non-error VkResult values wrapped as enum.
pub const Result = enum {
    success,
    not_ready,
    timeout,
    event_set,
    event_reset,
    incomplete,
    pipeline_compile_required,
    suboptimal_khr,
    thread_idle_khr,
    thread_done_khr,
    operation_deferred_khr,
    operation_not_deferred_khr,
    incompatible_shader_binary_ext,
    pipeline_binary_missing_khr,
};

/// Error VkResult values wrapped as error.
pub const Error = error{
    OutOfHostMemory,
    OutOfDeviceMemory,
    InitializationFailed,
    DeviceLost,
    MemoryMapFailed,
    LayerNotPresent,
    ExtensionNotPresent,
    FeatureNotPresent,
    IncompatibleDriver,
    TooManyObjects,
    FormatNotSupported,
    FragmentedPool,
    Unknown,
    OutOfPoolMemory,
    InvalidExternalHandle,
    Fragmentation,
    InvalidOpaqueCaptureAddress,
    NotPermitted,
    SurfaceLostKHR,
    NativeWindowInUseKHR,
    OutOfDateKHR,
    IncompatibleDisplayKHR,
    ValidationFailedEXT,
    InvalidShaderNV,
    ImageUsageNotSupportedKHR,
    VideoPictureLayoutNotSupportedKHR,
    VideoProfileOperationNotSupportedKHR,
    VideoProfileFormatNotSupportedKHR,
    VideoProfileCodecNotSupportedKHR,
    VideoSTDVersionNotSupportedKHR,
    InvalidDRMFormatModifierPlaneLayoutEXT,
    FullScreenExclusiveModeLostEXT,
    InvalidVideoSTDParametersKHR,
    CompressionExhaustedEXT,
    NotEnoughSpaceKHR,
};

/// Cast c.VkResult value into vk.Result or vk.Error.
pub fn castResult(result: c.VkResult) !Result {
    return switch (result) {
        c.VK_SUCCESS => .success,
        c.VK_NOT_READY => .not_ready,
        c.VK_TIMEOUT => .timeout,
        c.VK_EVENT_SET => .event_set,
        c.VK_EVENT_RESET => .event_reset,
        c.VK_INCOMPLETE => .incomplete,
        c.VK_ERROR_OUT_OF_HOST_MEMORY => error.OutOfHostMemory,
        c.VK_ERROR_OUT_OF_DEVICE_MEMORY => error.OutOfDeviceMemory,
        c.VK_ERROR_INITIALIZATION_FAILED => error.InitializationFailed,
        c.VK_ERROR_DEVICE_LOST => error.DeviceLost,
        c.VK_ERROR_MEMORY_MAP_FAILED => error.MemoryMapFailed,
        c.VK_ERROR_LAYER_NOT_PRESENT => error.LayerNotPresent,
        c.VK_ERROR_EXTENSION_NOT_PRESENT => error.ExtensionNotPresent,
        c.VK_ERROR_FEATURE_NOT_PRESENT => error.FeatureNotPresent,
        c.VK_ERROR_INCOMPATIBLE_DRIVER => error.IncompatibleDriver,
        c.VK_ERROR_TOO_MANY_OBJECTS => error.TooManyObjects,
        c.VK_ERROR_FORMAT_NOT_SUPPORTED => error.FormatNotSupported,
        c.VK_ERROR_FRAGMENTED_POOL => error.FragmentedPool,
        c.VK_ERROR_UNKNOWN => error.Unknown,
        c.VK_ERROR_OUT_OF_POOL_MEMORY => error.OutOfPoolMemory,
        c.VK_ERROR_INVALID_EXTERNAL_HANDLE => error.InvalidExternalHandle,
        c.VK_ERROR_FRAGMENTATION => error.Fragmentation,
        c.VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS => error.InvalidOpaqueCaptureAddress,
        c.VK_PIPELINE_COMPILE_REQUIRED => .pipeline_compile_required,
        c.VK_ERROR_NOT_PERMITTED => error.NotPermitted,
        c.VK_ERROR_SURFACE_LOST_KHR => error.SurfaceLostKHR,
        c.VK_ERROR_NATIVE_WINDOW_IN_USE_KHR => error.NativeWindowInUseKHR,
        c.VK_SUBOPTIMAL_KHR => .suboptimal_khr,
        c.VK_ERROR_OUT_OF_DATE_KHR => error.OutOfDateKHR,
        c.VK_ERROR_INCOMPATIBLE_DISPLAY_KHR => error.IncompatibleDisplayKHR,
        c.VK_ERROR_VALIDATION_FAILED_EXT => error.ValidationFailedEXT,
        c.VK_ERROR_INVALID_SHADER_NV => error.InvalidShaderNV,
        c.VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR => error.ImageUsageNotSupportedKHR,
        c.VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR => error.VideoPictureLayoutNotSupportedKHR,
        c.VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR => error.VideoProfileOperationNotSupportedKHR,
        c.VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR => error.VideoProfileFormatNotSupportedKHR,
        c.VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR => error.VideoProfileCodecNotSupportedKHR,
        c.VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR => error.VideoSTDVersionNotSupportedKHR,
        c.VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT => error.InvalidDRMFormatModifierPlaneLayoutEXT,
        c.VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT => error.FullScreenExclusiveModeLostEXT,
        c.VK_THREAD_IDLE_KHR => .thread_idle_khr,
        c.VK_THREAD_DONE_KHR => .thread_done_khr,
        c.VK_OPERATION_DEFERRED_KHR => .operation_deferred_khr,
        c.VK_OPERATION_NOT_DEFERRED_KHR => .operation_not_deferred_khr,
        c.VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR => error.InvalidVideoSTDParametersKHR,
        c.VK_ERROR_COMPRESSION_EXHAUSTED_EXT => error.CompressionExhaustedEXT,
        c.VK_INCOMPATIBLE_SHADER_BINARY_EXT => .incompatible_shader_binary_ext,
        c.VK_PIPELINE_BINARY_MISSING_KHR => .pipeline_binary_missing_khr,
        c.VK_ERROR_NOT_ENOUGH_SPACE_KHR => error.NotEnoughSpaceKHR,
        else => error.Unknown,
    };
}

// Container-privates:
//
// Vulkan initialization constants:
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

fn containsBitFlag(a: c_uint, b: c_uint) bool {
    return a & b == b;
}

fn initInstance() !void {
    if (try castResult(c.volkInitialize()) != .success) return error.VulkanLoad;

    var glfw_ext_l: u32 = 0;
    const glfw_ext = c.glfwGetRequiredInstanceExtensions(@ptrCast(&glfw_ext_l));

    const extension_l = if (debug) glfw_ext_l + 1 else glfw_ext_l;

    const extensions = try allocator.alloc([*:0]const u8, extension_l);
    defer allocator.free(extensions);

    for (0..glfw_ext_l) |i| extensions[i] = glfw_ext[i];
    if (debug) extensions[glfw_ext_l] = c.VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    const instance_info = c.VkInstanceCreateInfo{
        .sType = c.VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = @ptrCast(&c.VkApplicationInfo{
            .sType = c.VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = api_version,
        }),
        .enabledExtensionCount = @intCast(extensions.len),
        .ppEnabledExtensionNames = @ptrCast(extensions.ptr),
    };

    _ = try castResult(c.vkCreateInstance.?(
        @ptrCast(&instance_info),
        null,
        @ptrCast(&instance),
    ));

    c.volkLoadInstanceOnly(instance);
}

fn initDebugMsg() !void {
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

    _ = try castResult(c.vkCreateDebugUtilsMessengerEXT.?(
        instance,
        @ptrCast(&debug_info),
        null,
        @ptrCast(&debug_msg),
    ));
}

fn initSurface(window: *c.GLFWwindow) !void {
    _ = try castResult(c.glfwCreateWindowSurface(
        instance,
        window,
        null,
        @ptrCast(&surface),
    ));
}

fn initDevice() !void {
    try pickDevice();
    try pickQueues();

    const queue_priority: f32 = 1.0;
    const unique_indices = try getUniqueQueueIndices();
    defer allocator.free(unique_indices);

    const queue_infos = try allocator.alloc(c.VkDeviceQueueCreateInfo, unique_indices.len);
    defer allocator.free(queue_infos);

    for (queue_infos, unique_indices) |*info, i| {
        info.* = c.VkDeviceQueueCreateInfo{
            .sType = c.VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = i,
            .queueCount = 1,
            .pQueuePriorities = @ptrCast(&queue_priority),
        };
    }

    const extensions = if (additional_support) blk: {
        var tmp = try allocator.alloc(
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
    } else try allocator.dupe([*:0]const u8, required_device_extensions);
    defer allocator.free(extensions);

    const device_info = c.VkDeviceCreateInfo{
        .sType = c.VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = @ptrCast(@constCast(&vulkan_features2)),
        .enabledExtensionCount = @intCast(extensions.len),
        .ppEnabledExtensionNames = @ptrCast(extensions.ptr),
        .queueCreateInfoCount = @intCast(queue_infos.len),
        .pQueueCreateInfos = @ptrCast(queue_infos.ptr),
    };

    _ = try castResult(c.vkCreateDevice.?(
        physical_device,
        @ptrCast(&device_info),
        null,
        @ptrCast(&device),
    ));

    c.volkLoadDevice(device);

    c.vkGetDeviceQueue.?(device, graphics.index, 0, @ptrCast(&graphics.handle));
    c.vkGetDeviceQueue.?(device, compute.index, 0, @ptrCast(&compute.handle));
    c.vkGetDeviceQueue.?(device, present.index, 0, @ptrCast(&present.handle));
    c.vkGetDeviceQueue.?(device, transfer.index, 0, @ptrCast(&transfer.handle));
}

fn initVma() !void {
    const functions = c.VmaVulkanFunctions{
        .vkGetInstanceProcAddr = c.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = c.vkGetDeviceProcAddr,
    };

    const vma_info = c.VmaAllocatorCreateInfo{
        .flags = c.VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physical_device,
        .device = device,
        .instance = instance,
        .pVulkanFunctions = @ptrCast(&functions),
    };

    _ = try castResult(c.vmaCreateAllocator(@ptrCast(&vma_info), @ptrCast(&vma_allocator)));
}

fn pickDevice() !void {
    var device_l: u32 = 0;
    _ = try castResult(c.vkEnumeratePhysicalDevices.?(
        instance,
        @ptrCast(&device_l),
        null,
    ));

    const devices = try allocator.alloc(c.VkPhysicalDevice, device_l);
    defer allocator.free(devices);

    _ = try castResult(c.vkEnumeratePhysicalDevices.?(
        instance,
        @ptrCast(&device_l),
        @ptrCast(devices.ptr),
    ));

    var weak_pick: ?c.VkPhysicalDevice = null;
    var best_pick: ?c.VkPhysicalDevice = null;
    for (devices) |pdev| {
        var extension_l: u32 = 0;
        _ = try castResult(c.vkEnumerateDeviceExtensionProperties.?(
            pdev,
            null,
            @ptrCast(&extension_l),
            null,
        ));

        const extensions = try allocator.alloc(c.VkExtensionProperties, extension_l);
        defer allocator.free(extensions);

        _ = try castResult(c.vkEnumerateDeviceExtensionProperties.?(
            pdev,
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
            weak_pick = pdev;
        } else if (found_required_extensions == required_device_extensions.len and
            found_additional_extensions == additional_device_extensions.len)
        {
            best_pick = pdev;
        }
    }

    if (best_pick) |best| {
        physical_device = best;
        additional_support = true;
    } else if (weak_pick) |weak| {
        physical_device = weak;
        additional_support = false;
    } else {
        return error.VulkanNoRequiredDevice;
    }
}

fn pickQueues() !void {
    var queue_l: u32 = 0;
    c.vkGetPhysicalDeviceQueueFamilyProperties.?(physical_device, @ptrCast(&queue_l), null);

    const properties = try allocator.alloc(c.VkQueueFamilyProperties, queue_l);
    defer allocator.free(properties);

    c.vkGetPhysicalDeviceQueueFamilyProperties.?(
        physical_device,
        @ptrCast(&queue_l),
        @ptrCast(properties.ptr),
    );

    var _graphics: ?Queue = null;
    var _compute: ?Queue = null;
    var _present: ?Queue = null;
    var _transfer: ?Queue = null;

    for (properties, 0..) |prop, i| {
        const index: u32 = @intCast(i);
        if (_graphics == null and containsBitFlag(prop.queueFlags, c.VK_QUEUE_GRAPHICS_BIT)) {
            _graphics = .{ .index = index };
        } else if (_compute == null and containsBitFlag(prop.queueFlags, c.VK_QUEUE_COMPUTE_BIT)) {
            _compute = .{ .index = index };
        } else if (_transfer == null and containsBitFlag(prop.queueFlags, c.VK_QUEUE_TRANSFER_BIT)) {
            _transfer = .{ .index = index };
        }

        if (_present == null and try canQueuePresent(index))
            _present = .{ .index = index };
        if (_graphics != null and _compute != null and _present != null and _transfer != null)
            break;
    }

    // Perform checks and search for fallback non-independent queues
    if (_graphics == null and _present == null) return error.VulkanNoRequiredQueues;
    if (_compute == null) {
        if (findQueueFlag(properties, c.VK_QUEUE_COMPUTE_BIT)) |index| {
            _compute = .{ .index = index };
        } else {
            return error.VulkanNoRequiredQueues;
        }
    }
    if (_transfer == null) {
        if (findQueueFlag(properties, c.VK_QUEUE_TRANSFER_BIT)) |index| {
            _transfer = .{ .index = index };
        } else {
            return error.VulkanNoRequiredQueues;
        }
    }

    graphics = _graphics.?;
    compute = _compute.?;
    present = _present.?;
    transfer = _transfer.?;
}

fn getUniqueQueueIndices() ![]u32 {
    const all = [_]u32{
        graphics.index,
        compute.index,
        present.index,
        transfer.index,
    };
    var unique = try std.ArrayList(u32).initCapacity(allocator, 4);

    for (all) |i| {
        for (unique.items) |ui| {
            if (i == ui) break;
        } else {
            try unique.append(i);
        }
    }

    return try unique.toOwnedSlice();
}

fn canQueuePresent(index: u32) !bool {
    var can_present = c.VK_FALSE;

    _ = try castResult(c.vkGetPhysicalDeviceSurfaceSupportKHR.?(
        physical_device,
        index,
        surface,
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
