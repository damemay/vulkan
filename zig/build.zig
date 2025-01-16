const std = @import("std");

fn cmake(b: *std.Build, d: *std.Build.Dependency) struct { *std.Build.Step.Run, *std.Build.Step.Run } {
    const root_path = d.path("").getPath(b);
    const build_path = d.path("build").getPath(b);
    const cmake_prebuild = b.addSystemCommand(&.{
        "cmake",
        "-B",
        build_path,
        "-S",
        root_path,
    });
    const cmake_build = b.addSystemCommand(&.{
        "cmake",
        "--build",
        build_path,
        "-j",
    });
    return .{ cmake_prebuild, cmake_build };
}

const Dependency = struct {
    build_dep: *std.Build.Dependency,
    lib_name: []const u8,
    lib_path: []const u8,
    include_path: []const u8,

    const Self = Dependency;

    fn init(
        b: *std.Build,
        dep: []const u8,
        lib_name: []const u8,
        lib_path: []const u8,
        include_path: []const u8,
    ) Self {
        var self: Self = undefined;
        self.build_dep = b.dependency(dep, .{});
        self.lib_name = lib_name;
        self.lib_path = lib_path;
        self.include_path = include_path;
        return self;
    }

    fn cmakeDependOn(self: Self, b: *std.Build, c: *std.Build.Step.Compile) void {
        const _cmake = cmake(b, self.build_dep);
        c.step.dependOn(&_cmake.@"0".step);
        c.step.dependOn(&_cmake.@"1".step);
    }

    fn cmakeDependOnWithArgs(
        self: Self,
        b: *std.Build,
        c: *std.Build.Step.Compile,
        args: []const []const u8,
    ) void {
        const _cmake = cmake(b, self.build_dep);
        _cmake.@"0".addArgs(args);
        c.step.dependOn(&_cmake.@"0".step);
        c.step.dependOn(&_cmake.@"1".step);
    }

    fn link(self: Self, c: *std.Build.Step.Compile) void {
        c.addLibraryPath(self.build_dep.path(self.lib_path));
        c.addIncludePath(self.build_dep.path(self.include_path));
        c.linkSystemLibrary(self.lib_name);
    }
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Lib
    const lib = b.addStaticLibrary(.{
        .name = "viz",
        .root_source_file = b.path("src/viz.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Lib dependencies
    const volk = Dependency.init(b, "volk", "volk", "build", "");
    volk.cmakeDependOn(b, lib);

    const glfw = Dependency.init(b, "glfw", "glfw3", "build/src", "include");
    glfw.cmakeDependOnWithArgs(b, lib, &.{
        "-DGLFW_BUILD_EXAMPLES=OFF",
        "-DGLFW_BUILD_TESTS=OFF",
        "-DGLFW_BUILD_DOCS=OFF",
        "-DGLFW_INSTALL=OFF",
    });

    volk.link(lib);
    glfw.link(lib);

    const vma = b.addStaticLibrary(.{
        .name = "vma",
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    vma.linkLibCpp();
    const vma_dep = b.dependency("vma", .{});
    const vma_include_path = vma_dep.path("include");
    vma.addCSourceFile(.{ .file = b.path("src/vma.cc"), .flags = &.{"-std=c++17"} });
    vma.addIncludePath(vma_include_path);
    lib.addIncludePath(vma_include_path);
    lib.linkLibrary(vma);

    b.installArtifact(vma);
    b.installArtifact(lib);

    // Test lib
    const tests = b.addTest(.{
        .root_source_file = b.path("src/viz.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    volk.link(tests);
    glfw.link(tests);
    tests.addIncludePath(vma_include_path);
    tests.linkLibrary(vma);

    const run_test = b.addRunArtifact(tests);
    run_test.has_side_effects = true;
    run_test.step.dependOn(&lib.step);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_test.step);

    // Exe
    const exe = b.addExecutable(.{
        .name = "zig",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    volk.link(exe);
    glfw.link(exe);
    exe.addIncludePath(vma_include_path);
    exe.linkLibrary(vma);
    b.installArtifact(exe);

    // Run exe
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);

    // ZLS Check
    const check = b.step("check", "Run compilation check");
    check.dependOn(&exe.step);
}
