const std = @import("std");
pub const App = @import("App.zig");
pub const vulkan = @import("vulkan.zig");
const glfw = @import("mach-glfw");
const vk = @import("vulkan");

pub fn main() !void {
    const app = try App.init(.{ .debug = true, .gpu_choice = .minimum }, std.heap.c_allocator);
    defer app.deinit();
}
