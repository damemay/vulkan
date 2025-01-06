// Exports
pub const c = @import("c.zig");
pub const vk = @import("vk.zig");
pub const App = @import("App.zig");

const std = @import("std");

pub fn containsBitFlag(a: c_uint, b: c_uint) bool {
    return a & b == b;
}

// Tests
const testing = @import("std").testing;
test "app init test" {
    const app = try App.init(.{ .debug = true }, std.testing.allocator);
    defer app.deinit();
}
