// The game calls it statsmenu.
#include <Interface/Menus/SkillsMenu.h>

static TiltedPhoques::Initializer s_skillsMenuInit(
    []()
    {
        // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/StatsMenuEx.cpp
        // Hoooks from souls RE
#ifdef SKYRIMVR
        // VR gets one of the four patches: no freeze frame background, which VR never draws (the menu stays black).
        // The other three patches have AE-only offsets.
        // SE 51638 (StatsMenu::ProcessMessage) is VR 0x8ec3e0, with the flag write at +0xBB6 (AE: +0xA10).
        // The bytes are checked before patching.
        VersionDbPtr<uint8_t> ProcessMessage(51638);
        constexpr size_t cFreezeFrameFlag = 0xBB6;
        // or dword ptr [rsi+0x1C], 0x20 -> kFreezeFrameBackground on the menu's flags
        constexpr uint8_t cExpected[]{0x83, 0x4E, 0x1C, 0x20};

        uint8_t* pFlagWrite = ProcessMessage.Get() + cFreezeFrameFlag;
        if (std::memcmp(pFlagWrite, cExpected, sizeof(cExpected)) == 0)
        {
            TiltedPhoques::Nop(pFlagWrite, sizeof(cExpected));
            spdlog::info("Skills menu: freeze frame background disabled, so the menu can run unpaused");
        }
        else
        {
            spdlog::error("Skills menu: {} is not the freeze frame flag write ({:02x} {:02x} {:02x} {:02x}), leaving it alone; "
                          "the menu will be black while unpaused",
                          fmt::ptr(pFlagWrite), pFlagWrite[0], pFlagWrite[1], pFlagWrite[2], pFlagWrite[3]);
        }
#else
        // Fix for menu not appearing
        VersionDbPtr<uint8_t> ProcessMessage(52510);
        TiltedPhoques::Nop(ProcessMessage.Get() + 0x84E, 6);
        // Prevent setting kFreezeFrameBackground flag
        TiltedPhoques::Nop(ProcessMessage.Get() + 0xA10, 4);
        // Keep the menu updated
        TiltedPhoques::Nop(ProcessMessage.Get() + 0x1040, 2);

        // Fix for controls not working
        VersionDbPtr<uint8_t> controlPatch(52518);
        TiltedPhoques::Nop(controlPatch.Get() + 0x46, 4);
        TiltedPhoques::Nop(controlPatch.Get() + 0x4A, 2);
#endif
    });
