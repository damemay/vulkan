const std = @import("std");
const glfw = @import("mach-glfw");
const vulkan = @import("vulkan.zig");

const Self = @This();

// Fields
allocator: std.mem.Allocator,
window: glfw.Window,
vk: vulkan.BaseHandles,
debug: bool,

pub const GpuChoice = enum { minimum, additional };

pub const InitOptions = struct {
    title: [*:0]const u8 = "zoe",
    width: u32 = 1280,
    height: u32 = 720,

    gpu_choice: GpuChoice = .additional,
    debug: bool = false,
};

pub fn init(opt: InitOptions, allocator: std.mem.Allocator) !Self {
    var self: Self = undefined;
    self.allocator = allocator;
    self.debug = true;

    try self.initGlfw(opt);
    errdefer {
        self.window.destroy();
        glfw.terminate();
    }

    self.vk = try vulkan.BaseHandles.init(opt, self.window, allocator);

    return self;
}

pub fn deinit(self: Self) void {
    self.vk.deinit();
    self.window.destroy();
    glfw.terminate();
}

fn initGlfw(self: *Self, opt: InitOptions) !void {
    if (!glfw.init(.{})) return glfw.getErrorCode();
    errdefer glfw.terminate();

    self.window = glfw.Window.create(opt.width, opt.height, opt.title, null, null, .{
        .client_api = .no_api,
        .visible = false,
    }) orelse return glfw.getErrorCode();
}
