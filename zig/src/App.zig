//! Structure holding and managing application state.

const std = @import("std");

const c = @import("c.zig");
const vk = @import("vk.zig");

const Self = @This();

// App fields:
allocator: std.mem.Allocator,
window: *c.GLFWwindow,
frames: vk.Frames,
// App state:
debug: bool,
additional_support: bool,

// App initialization
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

    try vk.init(opt, self.window, allocator);
    errdefer vk.deinit();

    self.frames = try vk.Frames.init(vk.graphics_queue.index, 2);
    errdefer self.frames.deinit();

    return self;
}

pub fn deinit(self: Self) void {
    self.frames.deinit();
    vk.deinit();
    c.glfwDestroyWindow(self.window);
    c.glfwTerminate();
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
// App initialization end.
