const std = @import("std");

fn cmake(b: *std.Build, d: *std.Build.Dependency) struct { *std.Build.Step.Run, *std.Build.Step.Run } {
    const root_path = d.path("").getPath(b);
    const build_path = d.path("build").getPath(b);
    const cmake_prebuild = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "-B",
        build_path,
        "-S",
        root_path,
    });
    const cmake_build = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "--build",
        build_path,
        "-j",
    });
    return .{ cmake_prebuild, cmake_build };
}

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
    const volk_dep = b.dependency("volk", .{});
    const volk_cmake = cmake(b, volk_dep);
    lib.step.dependOn(&volk_cmake.@"0".step);
    lib.step.dependOn(&volk_cmake.@"1".step);
    lib.addLibraryPath(volk_dep.path("build"));
    lib.addIncludePath(volk_dep.path(""));
    lib.linkSystemLibrary("volk");

    const glfw_dep = b.dependency("glfw", .{});
    const glfw_cmake = cmake(b, glfw_dep);
    glfw_cmake.@"0".addArgs(&[_][]const u8{
        "-DGLFW_BUILD_EXAMPLES=OFF",
        "-DGLFW_BUILD_TESTS=OFF",
        "-DGLFW_BUILD_DOCS=OFF",
        "-DGLFW_INSTALL=OFF",
    });
    lib.step.dependOn(&glfw_cmake.@"0".step);
    lib.step.dependOn(&glfw_cmake.@"1".step);

    lib.addLibraryPath(glfw_dep.path("build/src"));
    lib.addIncludePath(glfw_dep.path("include"));
    lib.linkSystemLibrary("glfw3");

    b.installArtifact(lib);

    // Test lib
    const tests = b.addTest(.{
        .root_source_file = b.path("src/viz.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    tests.addIncludePath(volk_dep.path(""));
    tests.addLibraryPath(volk_dep.path("build"));
    tests.linkSystemLibrary("volk");
    tests.addIncludePath(glfw_dep.path(""));
    tests.addLibraryPath(glfw_dep.path("build/src"));
    tests.linkSystemLibrary("glfw3");

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

    exe.addIncludePath(volk_dep.path(""));
    exe.addIncludePath(glfw_dep.path(""));
    b.installArtifact(exe);

    // Run exe
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
