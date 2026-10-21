
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../client", "../server")
    add_headerfiles("**.h")
    add_files("*.cpp")
    -- The public server's admin keys: pure code, the hash handed in (no OpenSSL here).
    add_files("../server/AdminKeys.cpp")
    -- The client's remote playback timing has no game dependencies.
    add_files("../client/Systems/PlaybackClock.cpp")
    -- Who may revive whom (ReviveRules): plain values in and out.
    add_files("../server/ReviveRules.cpp")
    add_deps("SkyrimEncoding")
    if is_plat("windows") then
        -- SmallDump.h (client) writes minidumps.
        add_syslinks("dbghelp")
        -- The VR banner's pictures are drawn with GDI, in the game's fonts read from its zlib-compressed font library.
        add_files("../client/Games/Skyrim/BannerPicture.cpp")
        add_syslinks("gdi32", "user32")
        add_packages("zlib")
    else
        -- Windows minidumps: on Linux this file broke the server's CI build from 2026-10-07 (c5d3236e).
        remove_files("smalldump.cpp")
        remove_files("banner.cpp")
        -- PatchCheck.h asks Windows what memory a call leads to.
        remove_files("patch_check.cpp")
    end
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm")
