set_xmakever("3.0.0")

-- If newer version of xmake, remove ccache until it actually works
if set_policy ~= nil then
    set_policy("build.ccache", false)
end

-- c code will use c99,
set_languages("c99", "cxx20")

if is_plat("windows") then
    add_cxflags("/bigobj")
    add_syslinks("kernel32")
    set_arch("x64")
    -- MERGE NOTE 2026-09-22: upstream switched the C runtime to static (MT). Every cached dependency was built
    -- against the dynamic one, so taking it rebuilds and re-downloads the whole package set, and openvr, CEF,
    -- protobuf and directxtk all failed to install here. It is also an ABI change for a DLL that lives inside
    -- the game next to other SKSE plugins. Left off until it can be done deliberately, with a clean build.
    -- set_runtimes("MT")
end

if is_plat("linux") then
    add_cxflags("-fPIC")
end

set_warnings("all")
add_vectorexts("sse", "sse2", "sse3", "ssse3")
add_vectorexts("neon")

-- build configurations
add_rules("mode.debug", "mode.releasedbg", "mode.release")

if has_config("unitybuild") then
    add_rules("c.unity_build")
    add_rules("c++.unity_build", {batchsize = 12})
end

-- Local package overrides, must be registered before anything resolves packages.
-- Currently only carries a fork of the cef recipe; see xmake-repo/packages/c/cef.
add_repositories("tilted-local-repo xmake-repo")

-- direct dependencies version pinning
add_requires(
    "entt v3.10.0", 
    "recastnavigation v1.6.0", 
    "tiltedcore v0.2.9",
    "cryptopp 8.9.0", 
    "spdlog v1.13.0", 
    "cpp-httplib 0.14.0",
    "gtest v1.14.0", 
    "mem 1.0.0", 
    "glm 0.9.9+8", 
    "zlib v1.3.1"
)
if is_plat("windows") then
    add_requires(
        "discord 3.2.1", 
        "imgui v1.89.7"
    )
end

-- dependencies' dependencies version pinning
add_requireconfs("*.mimalloc", { version = "2.2.4", override = true })
add_requireconfs("*.cmake", { version = "3.30.2", override = true })
add_requireconfs("*.openssl", { version = "1.1.1-w", override = true })
add_requireconfs("*.zlib", { version = "v1.3.1", override = true })
if is_plat("linux") then
    add_requireconfs("*.libcurl", { version = "8.7.1", override = true })
end

add_requireconfs("cpp-httplib", {configs = {ssl = true}})
--[[
add_requireconfs("magnum", { configs = { sdl2 = true }})
add_requireconfs("magnum-integration",  { configs = { imgui = true }})
add_requireconfs("magnum-integration.magnum",  { configs = { sdl2 = true }})
add_requireconfs("magnum-integration.imgui", { override = true })
--]]

-- The version, as compile defines set when each target loads, computed once per xmake run and cached in _g.
-- A define is part of xmake's dependency hash, so a changed version recompiles what embeds it.
-- A generated header would not do: xmake does not rebuild for a header changed during the build.
on_load(function (target)
    if not _g.tilted_version then
        import("modules.version")
        local branch, commitHash, timestamp, describe, protocol = version()
        _g.tilted_version = { branch = branch, describe = describe, protocol = protocol }
    end
    if not _g.tilted_version.numbers then
        -- The same version as four numbers, the tag then the commits since it (v1.9.0-2-g827b3940 is 1.9.0.2); 0.0.0.0
        -- without a tag.
        local describe = _g.tilted_version.describe
        local major, minor, patch = describe:match("^v?(%d+)%.(%d+)%.(%d+)")
        _g.tilted_version.numbers = { major or 0, minor or 0, patch or 0, (major and describe:match("^v?%d+%.%d+%.%d+%-(%d+)%-g")) or 0 }
    end
    -- What the server compares on connect: a digest of Code/encoding alone. See modules/version.lua.
    target:add("defines", 'BUILD_PROTOCOL="' .. _g.tilted_version.protocol .. '"')
    -- For the programs' version resources (the .rc files) and the server's status page; these change only with a tag.
    -- Antivirus engines weigh an unsigned program with no believable identity as suspicious, so the resources carry the real version.
    local n = _g.tilted_version.numbers
    target:add("defines", "BUILD_MAJOR=" .. n[1], "BUILD_MINOR=" .. n[2], "BUILD_PATCH=" .. n[3])
    -- The commit changes with every edit (the describe ends -dirty.<hash>), and the commit count with every commit.
    -- A define set on every file would recompile every file for each of those changes.
    -- So these are set only on Code/common/BuildVersion.cpp, which the code reads them from (BuildVersion.h), and on the resource files.
    local versionDefines = {'BUILD_BRANCH="' .. _g.tilted_version.branch .. '"', 'BUILD_COMMIT="' .. _g.tilted_version.describe .. '"',
        "BUILD_SINCE=" .. n[4], 'BUILD_FILEVERSION="' .. table.concat(n, ".") .. '"'}
    local versionFiles = os.files(path.join(target:scriptdir(), "*.rc"))
    table.insert(versionFiles, path.join(os.projectdir(), "Code", "common", "BuildVersion.cpp"))
    for _, file in ipairs(versionFiles) do
        target:fileconfig_add(path.relative(file, os.projectdir()), {defines = versionDefines})
    end
end)

before_build(function (target)
    import("modules.version")
    local branch, commitHash, timestamp, describe = version()
    bool_to_number={ [true]=1, [false]=0 }

    -- The version string for tooling (Tools\VR\make-release.ps1 names the zips after it). BuildInfo.h only holds
    -- fallbacks since the real value became a compile define, which left the release named "unknown-version".
    local versionPath = "build/BuildVersion.txt"
    if not os.exists(versionPath) or io.readfile(versionPath) ~= describe then
        io.writefile(versionPath, describe)
    end
    local contents = string.format([[
    #pragma once
    #define IS_MASTER %d
    #define IS_BRANCH_BETA %d
    #define IS_BRANCH_PREREL %d
    ]],
    bool_to_number[branch == "master"],
    bool_to_number[branch == "bluedove"],
    bool_to_number[branch == "prerel"])

    -- fix always-compiles problem by updating the file only if content has changed.
    local filepath = "build/BranchInfo.h"
    local old_content = nil
    if os.exists(filepath) then
        old_content = io.readfile(filepath)
    end
    if old_content ~= contents then
        print("Updating file:", filepath)
        io.writefile(filepath, contents)
    end

    -- BuildInfo.h is still included by the sources; it only carries fallbacks now, the values are the defines above.
    -- The commit, branch and commit count have none: code reads them from BuildVersion.h, and a use left of the macros fails to build.
    local buildInfoPath = "build/BuildInfo.h"
    local buildInfo = [[
#pragma once
// Generated by xmake (root xmake.lua). BUILD_PROTOCOL and BUILD_MAJOR/MINOR/PATCH are compile defines; these are fallbacks.
// BUILD_COMMIT, BUILD_BRANCH, BUILD_SINCE and BUILD_FILEVERSION are set only on BuildVersion.cpp and the .rc files.
#ifndef BUILD_PROTOCOL
#define BUILD_PROTOCOL "unknown-protocol"
#endif
#ifndef BUILD_MAJOR
#define BUILD_MAJOR 0
#define BUILD_MINOR 0
#define BUILD_PATCH 0
#endif
]]
    if not os.exists(buildInfoPath) or io.readfile(buildInfoPath) ~= buildInfo then
        io.writefile(buildInfoPath, buildInfo)
    end
end)

if is_mode("debug") then
    add_defines("DEBUG")
end

if is_plat("windows") then
    add_defines("NOMINMAX")
end

-- add projects
includes("Libraries")
includes("Code")
