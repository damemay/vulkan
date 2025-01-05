const std = @import("std");
const c = @import("c.zig");

pub const VulkanQueue = struct {
    index: u32,
    handle: c.VkQueue = null,
};

pub fn containsBitFlag(a: c_uint, b: c_uint) bool {
    return a & b == b;
}

pub fn notVkSuccess(vk_result: c.VkResult) bool {
    if (vk_result != c.VK_SUCCESS) return true;
    return false;
}

pub fn debugCallback(
    _: c.VkDebugUtilsMessageSeverityFlagBitsEXT,
    _: c.VkDebugUtilsMessageTypeFlagsEXT,
    data: *c.VkDebugUtilsMessengerCallbackDataEXT,
    _: ?*anyopaque,
) callconv(.C) c.VkBool32 {
    std.debug.print("{s}\n", .{data.pMessage});
    return c.VK_FALSE;
}
