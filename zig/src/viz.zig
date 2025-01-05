const std = @import("std");
pub const c = @import("c.zig");
pub const init = @import("init.zig");

const testing = @import("std").testing;
test "app init test" {
    const app = try init.App.init(.{
        .title = "",
        .width = 640,
        .height = 480,
        .debug = true,
    }, std.testing.allocator);
    defer app.deinit();

    c.glfwSetWindowShouldClose(app.window, c.GLFW_TRUE);
}
