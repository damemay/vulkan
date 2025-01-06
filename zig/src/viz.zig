// Exports
pub const c = @import("c.zig");
pub const vk = @import("vk.zig");

pub const AppInitError = app_init.Error;
pub const AppInitOptions = app_init.Options;
//
const app_init = @import("app_init.zig");
const std = @import("std");

pub const Queue = struct {
    index: u32,
    handle: c.VkQueue = null,
};

pub const App = struct {
    window: *c.GLFWwindow,
    allocator: std.mem.Allocator,
    // Vulkan handles
    instance: c.VkInstance,
    debug_msg: c.VkDebugUtilsMessengerEXT,
    surface: c.VkSurfaceKHR,
    pdev: c.VkPhysicalDevice,
    dev: c.VkDevice,
    swapchain: c.VkSwapchainKHR,
    vma: c.VmaAllocator,
    // Vulkan Swapchain data
    present_mode: c.VkPresentModeKHR,
    format: c.VkFormat,
    color_space: c.VkColorSpaceKHR,
    current_extent: c.VkExtent2D,
    swapchain_images: []c.VkImage,
    swapchain_image_views: []c.VkImageView,
    // Vulkan Queues
    graphics: Queue,
    compute: Queue,
    present: Queue,
    transfer: Queue,
    // App state
    debug: bool,
    additional_support: bool,

    const Self = App;

    pub fn init(opt: app_init.Options, allocator: std.mem.Allocator) !Self {
        var self: Self = undefined;
        self.allocator = allocator;
        self.debug = opt.debug;

        try app_init.glfw(&self, opt);
        errdefer {
            c.glfwDestroyWindow(self.window);
            c.glfwTerminate();
        }

        try app_init.instance(&self);
        errdefer c.vkDestroyInstance.?(self.instance, null);

        if (opt.debug) try app_init.debugMsg(&self);
        errdefer if (opt.debug)
            c.vkDestroyDebugUtilsMessengerEXT.?(self.instance, self.debug_msg, null);

        try app_init.surface(&self);
        errdefer c.vkDestroySurfaceKHR.?(self.instance, self.surface, null);

        try app_init.device(&self);
        errdefer c.vkDestroyDevice.?(self.dev, null);

        try app_init.swapchain(&self, opt);
        errdefer self.destroySwapchain();

        try app_init.vma(&self);
        errdefer c.vmaDestroyAllocator(self.vma);

        return self;
    }

    pub fn deinit(self: Self) void {
        c.vmaDestroyAllocator(self.vma);
        self.destroySwapchain();
        c.vkDestroyDevice.?(self.dev, null);
        c.vkDestroySurfaceKHR.?(self.instance, self.surface, null);
        if (self.debug) c.vkDestroyDebugUtilsMessengerEXT.?(self.instance, self.debug_msg, null);
        c.vkDestroyInstance.?(self.instance, null);
        c.glfwDestroyWindow(self.window);
        c.glfwTerminate();
    }

    pub fn createSwapchain(
        self: *Self,
        width: u32,
        height: u32,
        old: c.VkSwapchainKHR,
    ) !c.VkSwapchainKHR {
        var caps: c.VkSurfaceCapabilitiesKHR = undefined;
        _ = try vk.castResult(c.vkGetPhysicalDeviceSurfaceCapabilitiesKHR.?(
            self.pdev,
            self.surface,
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

        const unique_indices = try self.getUniqueSwapchainQueueIndices();
        defer self.allocator.free(unique_indices);

        const swapchain_info = c.VkSwapchainCreateInfoKHR{
            .sType = c.VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = self.surface,
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

        var swapchain: c.VkSwapchainKHR = undefined;
        _ = try vk.castResult(c.vkCreateSwapchainKHR.?(
            self.dev,
            @ptrCast(&swapchain_info),
            null,
            @ptrCast(&swapchain),
        ));

        var image_count: u32 = 0;
        _ = try vk.castResult(c.vkGetSwapchainImagesKHR.?(
            self.dev,
            swapchain,
            @ptrCast(&image_count),
            null,
        ));

        self.swapchain_images = try self.allocator.alloc(c.VkImage, image_count);
        _ = try vk.castResult(c.vkGetSwapchainImagesKHR.?(
            self.dev,
            swapchain,
            @ptrCast(&image_count),
            @ptrCast(self.swapchain_images.ptr),
        ));

        self.swapchain_image_views = try self.allocator.alloc(c.VkImageView, image_count);

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

            _ = try vk.castResult(c.vkCreateImageView.?(
                self.dev,
                @ptrCast(&imgv_info),
                null,
                @ptrCast(&imgv.*),
            ));
        }

        return swapchain;
    }

    pub fn destroySwapchain(self: Self) void {
        c.vkDestroySwapchainKHR.?(self.dev, self.swapchain, null);
        for (self.swapchain_image_views) |imgv| c.vkDestroyImageView.?(self.dev, imgv, null);
        self.allocator.free(self.swapchain_images);
        self.allocator.free(self.swapchain_image_views);
    }

    fn getUniqueSwapchainQueueIndices(self: Self) ![]u32 {
        const all = [_]u32{
            self.graphics.index,
            self.present.index,
        };
        var unique = try std.ArrayList(u32).initCapacity(self.allocator, 2);

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

pub fn containsBitFlag(a: c_uint, b: c_uint) bool {
    return a & b == b;
}

// Tests
const testing = @import("std").testing;
test "app init test" {
    const app = try App.init(.{ .debug = true }, std.testing.allocator);
    defer app.deinit();
}
