const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Option to enable internal test instrumentation (ltests.h)
    const enable_tests = b.option(bool, "tests", "Enable internal Lua test instrumentation (ltests.h)") orelse false;

    const lua_lib = buildLua(b, target, optimize, enable_tests);
    b.installArtifact(lua_lib);

    const lua_exe = buildInterpreter(b, target, optimize, lua_lib);
    b.installArtifact(lua_exe);

    // `zig build run` launches the interpreter
    const run_cmd = b.addRunArtifact(lua_exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    b.step("run", "Run the Lua interpreter").dependOn(&run_cmd.step);

    // Determinism test harness
    const det_test = buildDeterminismTest(b, target, optimize, lua_lib);
    b.installArtifact(det_test);

    // `zig build test-determinism` runs dual-state comparison
    const run_det = b.addRunArtifact(det_test);
    run_det.step.dependOn(b.getInstallStep());
    b.step("test-determinism", "Run dual-state determinism test").dependOn(&run_det.step);

    // `zig build test-determinism-golden` verifies against golden file
    const run_golden = b.addRunArtifact(det_test);
    run_golden.step.dependOn(b.getInstallStep());
    run_golden.addArgs(&.{ "--golden-verify", "testes/determinism_golden.txt" });
    b.step("test-determinism-golden", "Verify output against golden file").dependOn(&run_golden.step);
}

const core_sources = [_][]const u8{
    "lapi.c",
    "lcode.c",
    "lctype.c",
    "ldebug.c",
    "ldo.c",
    "ldump.c",
    "lfunc.c",
    "lgc.c",
    "llex.c",
    "lmem.c",
    "lobject.c",
    "lopcodes.c",
    "lparser.c",
    "lstate.c",
    "lstring.c",
    "ltable.c",
    "ltm.c",
    "lundump.c",
    "lvm.c",
    "lzio.c",
    "kulua_fixed.c",
};

const lib_sources = [_][]const u8{
    "lauxlib.c",
    "lbaselib.c",
    "lcorolib.c",
    "ldblib.c",
    "linit.c",
    "liolib.c",
    "lmathlib.c",
    "loadlib.c",
    "loslib.c",
    "lstrlib.c",
    "ltablib.c",
    "lutf8lib.c",
};

const c_flags: []const []const u8 = &.{
    "-std=c99",
    "-Wall",
    "-Wextra", "-Wno-unused-parameter",
    "-DLUA_FIXED_POINT",
};

const c_flags_tests: []const []const u8 = &.{
    "-std=c99",
    "-Wall",
    "-Wextra", "-Wno-unused-parameter",
    "-DLUA_FIXED_POINT",
    "-DLUA_USER_H=\"ltests.h\"",
};

fn buildLua(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    enable_tests: bool,
) *std.Build.Step.Compile {
    const lib = b.addLibrary(.{
        .name = "lua",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });

    const flags = if (enable_tests) c_flags_tests else c_flags;
    lib.addCSourceFiles(.{ .files = &core_sources, .flags = flags });
    lib.addCSourceFiles(.{ .files = &lib_sources, .flags = flags });

    if (enable_tests) {
        lib.addCSourceFiles(.{ .files = &.{"ltests.c"}, .flags = flags });
    }

    return lib;
}

fn buildDeterminismTest(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lua_lib: *std.Build.Step.Compile,
) *std.Build.Step.Compile {
    const exe = b.addExecutable(.{
        .name = "kulua_test_determinism",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });

    exe.addCSourceFiles(.{ .files = &.{"kulua_test_determinism.c"}, .flags = c_flags });
    exe.linkLibrary(lua_lib);

    return exe;
}

fn buildInterpreter(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lua_lib: *std.Build.Step.Compile,
) *std.Build.Step.Compile {
    const exe = b.addExecutable(.{
        .name = "lua",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });

    exe.addCSourceFiles(.{ .files = &.{"lua.c"}, .flags = c_flags });
    exe.linkLibrary(lua_lib);

    return exe;
}
