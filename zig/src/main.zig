const std = @import("std");
const viz = @import("viz.zig");

pub fn main() !void {
    const app = try viz.App.init();
    defer app.deinit();
}
