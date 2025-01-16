const std = @import("std");
const glfw = @import("mach-glfw");
const vk = @import("vulkan");
const App = @import("App.zig");

// Initialization constants
const api_version = vk.API_VERSION_1_2;
const format = vk.Format.b8g8r8a8_srgb;
const color_space = vk.ColorSpaceKHR.srgb_nonlinear_khr;
const required_device_extensions = [_][*:0]const u8{
    vk.extensions.khr_swapchain.name,
    vk.extensions.khr_push_descriptor.name,
};
const additional_device_extensions = [_][*:0]const u8{
    vk.extensions.khr_acceleration_structure.name,
    vk.extensions.khr_ray_tracing_pipeline.name,
    vk.extensions.khr_ray_query.name,
    vk.extensions.khr_pipeline_library.name,
    vk.extensions.khr_deferred_host_operations.name,
    vk.extensions.ext_mesh_shader.name,
};
const acceleration_structure_features = vk.PhysicalDeviceAccelerationStructureFeaturesKHR{
    .acceleration_structure = vk.TRUE,
};
const ray_tracing_pipeline_features = vk.PhysicalDeviceRayTracingPipelineFeaturesKHR{
    .p_next = @constCast(&acceleration_structure_features),
    .ray_tracing_pipeline = vk.TRUE,
};
const ray_query_features = vk.PhysicalDeviceRayQueryFeaturesKHR{
    .p_next = @constCast(&ray_tracing_pipeline_features),
    .ray_query = vk.TRUE,
};
const mesh_shader_features = vk.PhysicalDeviceMeshShaderFeaturesEXT{
    .p_next = @constCast(&ray_query_features),
    .mesh_shader = vk.TRUE,
    .task_shader = vk.TRUE,
};
var vulkan12_features = vk.PhysicalDeviceVulkan12Features{
    .p_next = @constCast(&mesh_shader_features),
    .imageless_framebuffer = vk.TRUE,
    .buffer_device_address = vk.TRUE,
    .separate_depth_stencil_layouts = vk.TRUE,
    .descriptor_indexing = vk.TRUE,
};
const vulkan_features2 = vk.PhysicalDeviceFeatures2{
    .p_next = &vulkan12_features,
    .features = .{ .sampler_anisotropy = vk.TRUE },
};

const apis: []const vk.ApiInfo = &.{
    vk.features.version_1_0,
    vk.features.version_1_1,
    vk.features.version_1_2,

    // Base extensions
    vk.extensions.khr_surface,
    vk.extensions.ext_debug_utils,
    vk.extensions.khr_swapchain,
    vk.extensions.khr_push_descriptor,
    // Additional extensions
    vk.extensions.khr_acceleration_structure,
    vk.extensions.khr_ray_tracing_pipeline,
    vk.extensions.khr_ray_query,
    vk.extensions.khr_pipeline_library,
    vk.extensions.khr_deferred_host_operations,
    vk.extensions.ext_mesh_shader,
};

const BaseDispatch = vk.BaseWrapper(apis);
const InstanceDispatch = vk.InstanceWrapper(apis);
const DeviceDispatch = vk.DeviceWrapper(apis);

const Instance = vk.InstanceProxy(apis);
const Device = vk.DeviceProxy(apis);

pub const Queue = struct {
    index: u32,
    handle: vk.Queue = .null_handle,
};

pub const DeviceError = error{
    NoGpuWithRequiredExtensions,
    NoGpuWithRequiredQueues,
};

pub const BaseHandles = struct {
    allocator: std.mem.Allocator,

    base: BaseDispatch,
    instance: Instance,

    debug_msg: vk.DebugUtilsMessengerEXT,

    physical_device: vk.PhysicalDevice,
    device: Device,

    surface: vk.SurfaceKHR,
    swapchain: vk.SwapchainKHR,

    graphics_queue: Queue,
    compute_queue: Queue,
    transfer_queue: Queue,

    debug: bool,
    additional_support: bool,

    const Self = @This();

    pub fn init(opt: App.InitOptions, window: glfw.Window, allocator: std.mem.Allocator) !Self {
        var self: Self = undefined;
        self.allocator = allocator;
        self.debug = opt.debug;

        try self.initInstance();
        errdefer {
            self.instance.destroyInstance(null);
            self.allocator.destroy(self.instance.wrapper);
        }

        if (opt.debug) try self.initDebug();
        errdefer if (opt.debug) self.instance.destroyDebugUtilsMessengerEXT(self.debug_msg, null);

        try self.initSurface(window);
        errdefer self.instance.destroySurfaceKHR(self.surface, null);

        try self.pickPhysicalDevice(opt);
        try self.findQueues();

        try self.initDevice();
        errdefer {
            self.device.destroyDevice(null);
            self.allocator.destroy(self.device.wrapper);
        }

        return self;
    }

    pub fn deinit(self: Self) void {
        self.device.destroyDevice(null);
        self.allocator.destroy(self.device.wrapper);

        self.instance.destroySurfaceKHR(self.surface, null);
        if (self.debug) self.instance.destroyDebugUtilsMessengerEXT(self.debug_msg, null);

        self.instance.destroyInstance(null);
        self.allocator.destroy(self.instance.wrapper);
    }

    fn initInstance(self: *Self) !void {
        self.base = try BaseDispatch.load(
            @as(vk.PfnGetInstanceProcAddr, @ptrCast(&glfw.getInstanceProcAddress)),
        );

        const glfw_extensions = glfw.getRequiredInstanceExtensions() orelse
            return glfw.getErrorCode();

        const extension_l = if (self.debug) glfw_extensions.len + 1 else glfw_extensions.len;
        const extensions = try self.allocator.alloc([*:0]const u8, extension_l);
        defer self.allocator.free(extensions);

        for (0..glfw_extensions.len) |i| extensions[i] = glfw_extensions[i];
        if (self.debug) extensions[glfw_extensions.len] = vk.extensions.ext_debug_utils.name;

        const instance = try self.base.createInstance(&.{
            .p_application_info = &.{
                .application_version = vk.makeApiVersion(0, 0, 0, 0),
                .engine_version = vk.makeApiVersion(0, 0, 0, 0),
                .api_version = api_version,
            },
            .enabled_extension_count = @intCast(extension_l),
            .pp_enabled_extension_names = @ptrCast(extensions),
        }, null);

        const wrapper = try self.allocator.create(InstanceDispatch);
        errdefer self.allocator.destroy(wrapper);

        wrapper.* = try InstanceDispatch.load(instance, self.base.dispatch.vkGetInstanceProcAddr);
        self.instance = Instance.init(instance, wrapper);
    }

    fn initDebug(self: *Self) !void {
        self.debug_msg = try self.instance.createDebugUtilsMessengerEXT(&.{
            .message_severity = .{
                .verbose_bit_ext = true,
                .info_bit_ext = true,
                .warning_bit_ext = true,
                .error_bit_ext = true,
            },
            .message_type = .{
                .general_bit_ext = true,
                .validation_bit_ext = true,
                .performance_bit_ext = true,
                .device_address_binding_bit_ext = true,
            },
            .pfn_user_callback = &debugCallback,
        }, null);
    }

    fn debugCallback(
        _: vk.DebugUtilsMessageSeverityFlagsEXT,
        _: vk.DebugUtilsMessageTypeFlagsEXT,
        data: ?*const vk.DebugUtilsMessengerCallbackDataEXT,
        _: ?*anyopaque,
    ) callconv(vk.vulkan_call_conv) vk.Bool32 {
        if (data) |d| if (d.p_message) |msg| std.debug.print("{s}\n", .{msg});
        return vk.FALSE;
    }

    fn initSurface(self: *Self, window: glfw.Window) !void {
        if (glfw.createWindowSurface(self.instance.handle, window, null, &self.surface) != 0)
            return glfw.getErrorCode();
    }

    fn pickPhysicalDevice(self: *Self, opt: App.InitOptions) !void {
        const pdevs = try self.instance.enumeratePhysicalDevicesAlloc(self.allocator);
        defer self.allocator.free(pdevs);

        var min_pick: ?vk.PhysicalDevice = null;
        var add_pick: ?vk.PhysicalDevice = null;

        for (pdevs) |pdev| {
            const exts = try self.instance
                .enumerateDeviceExtensionPropertiesAlloc(pdev, null, self.allocator);
            defer self.allocator.free(exts);

            var min_exts: u32 = 0;
            var add_exts: u32 = 0;

            for (exts) |ext| {
                for (required_device_extensions) |req_ext| {
                    if (std.mem.orderZ(u8, @ptrCast(&ext.extension_name), req_ext) == .eq)
                        min_exts += 1;
                }
                for (additional_device_extensions) |add_ext| {
                    if (std.mem.orderZ(u8, @ptrCast(&ext.extension_name), add_ext) == .eq)
                        add_exts += 1;
                }
            }

            const properties = self.instance.getPhysicalDeviceProperties(pdev);
            if (properties.api_version <= api_version) break;

            if (min_exts == required_device_extensions.len and
                add_exts != additional_device_extensions.len) min_pick = pdev;
            if (min_exts == required_device_extensions.len and
                add_exts == additional_device_extensions.len) add_pick = pdev;
        }

        if (opt.gpu_choice == .additional) {
            if (add_pick) |pick| {
                self.physical_device = pick;
                self.additional_support = true;
            } else if (min_pick) |pick| {
                self.physical_device = pick;
                self.additional_support = false;
            } else return error.NoGpuWithRequiredExtensions;
        } else {
            if (min_pick) |pick| {
                self.physical_device = pick;
                self.additional_support = false;
            } else return error.NoGpuWithRequiredExtensions;
        }
    }

    fn findQueues(self: *Self) !void {
        const queue_props = try self.instance
            .getPhysicalDeviceQueueFamilyPropertiesAlloc(self.physical_device, self.allocator);
        defer self.allocator.free(queue_props);

        var graphics: ?Queue = null;
        var present: ?Queue = null;
        var compute: ?Queue = null;
        var transfer: ?Queue = null;

        for (queue_props, 0..) |props, i| {
            const index: u32 = @intCast(i);

            if (graphics == null and props.queue_flags.contains(.{ .graphics_bit = true })) {
                graphics = .{ .index = index };
            } else if (compute == null and props.queue_flags.contains(.{ .compute_bit = true })) {
                compute = .{ .index = index };
            } else if (transfer == null and props.queue_flags.contains(.{ .transfer_bit = true })) {
                transfer = .{ .index = index };
            }

            if (present == null and try self.instance.getPhysicalDeviceSurfaceSupportKHR(
                self.physical_device,
                index,
                self.surface,
            ) == vk.TRUE)
                present = .{ .index = index };

            if (graphics != null and compute != null and present != null and transfer != null) break;
        }

        // Assert graphics queue equals present queue
        if (graphics == null and present == null) return error.NoGpuWithRequiredQueues;
        if (graphics.?.index != present.?.index) return error.NoGpuWithRequiredQueues;

        if (compute == null) {
            if (findQueueFamilyIndexByFlags(queue_props, .{ .compute_bit = true })) |index| {
                compute = .{ .index = index };
            } else return error.NoGpuWithRequiredQueues;
        }

        if (transfer == null) {
            if (findQueueFamilyIndexByFlags(queue_props, .{ .transfer_bit = true })) |index| {
                transfer = .{ .index = index };
            } else return error.NoGpuWithRequiredQueues;
        }

        self.graphics_queue = graphics.?;
        self.compute_queue = compute.?;
        self.transfer_queue = transfer.?;
    }

    fn initDevice(self: *Self) !void {
        const queue_priority: f32 = 1.0;
        const all_queue = [_]u32{
            self.graphics_queue.index,
            self.compute_queue.index,
            self.transfer_queue.index,
        };

        var unique_queue = try std.ArrayList(u32).initCapacity(self.allocator, all_queue.len);
        defer unique_queue.deinit();

        for (all_queue) |idx| _ = try appendIfUnique(idx, &unique_queue);

        const queue_infos = try self.allocator.alloc(vk.DeviceQueueCreateInfo, unique_queue.items.len);
        defer self.allocator.free(queue_infos);

        for (queue_infos, unique_queue.items) |*info, i| {
            info.* = vk.DeviceQueueCreateInfo{
                .queue_family_index = i,
                .queue_count = 1,
                .p_queue_priorities = @ptrCast(&queue_priority),
            };
        }

        const extensions = if (self.additional_support) blk: {
            var tmp = try self.allocator.alloc(
                [*:0]const u8,
                required_device_extensions.len + additional_device_extensions.len,
            );
            std.mem.copyForwards([*:0]const u8, tmp[0..], &required_device_extensions);
            std.mem.copyForwards(
                [*:0]const u8,
                tmp[required_device_extensions.len..],
                &additional_device_extensions,
            );
            break :blk tmp;
        } else try self.allocator.dupe([*:0]const u8, &required_device_extensions);
        defer self.allocator.free(extensions);

        if (!self.additional_support) vulkan12_features.p_next = null;

        const device = try self.instance.createDevice(self.physical_device, &.{
            .p_next = &vulkan_features2,
            .enabled_extension_count = @intCast(extensions.len),
            .pp_enabled_extension_names = @ptrCast(extensions.ptr),
            .queue_create_info_count = @intCast(queue_infos.len),
            .p_queue_create_infos = @ptrCast(queue_infos.ptr),
        }, null);

        const wrapper = try self.allocator.create(DeviceDispatch);
        errdefer self.allocator.destroy(wrapper);
        wrapper.* = try DeviceDispatch.load(device, self.instance.wrapper.dispatch.vkGetDeviceProcAddr);
        self.device = Device.init(device, wrapper);

        self.graphics_queue.handle = self.device.getDeviceQueue(self.graphics_queue.index, 0);
        self.compute_queue.handle = self.device.getDeviceQueue(self.compute_queue.index, 0);
        self.transfer_queue.handle = self.device.getDeviceQueue(self.transfer_queue.index, 0);
    }
};

fn findQueueFamilyIndexByFlags(
    queue_properties: []vk.QueueFamilyProperties,
    flags: vk.QueueFlags,
) ?u32 {
    for (queue_properties, 0..) |props, i| {
        const index: u32 = @intCast(i);
        if (props.queue_flags.contains(flags)) return index;
    }
    return null;
}

fn appendIfUnique(val: anytype, list: *std.ArrayList(@TypeOf(val))) !bool {
    for (list.items) |i| {
        if (i == val) return false;
    } else {
        try list.append(val);
        return true;
    }
}
