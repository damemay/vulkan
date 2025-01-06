const std = @import("std");
const viz = @import("viz.zig");

pub fn main() !void {
    const app = try viz.App.init(.{
        .width = 1280,
        .height = 720,
        .debug = true,
    }, std.heap.c_allocator);
    defer app.deinit();
}
