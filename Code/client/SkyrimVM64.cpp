#include <Games/Skyrim/VRBodySync.h>
#include <Games/Skyrim/HeadPartWatch.h>
#include <Services/CharacterService.h>
#include <World.h>
#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"
#include <Misc/GameVM.h>
#include <Interface/Menus/SleepWaitMenu.h>
#include <Interface/Menus/SkillsMenu.h>
#include <Interface/Menus/MapMenu.h>
#include <Interface/UI.h>
#include <PatchCheck.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

struct Main;

TP_THIS_FUNCTION(TVMUpdate, int, GameVM, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

#ifdef SKYRIMVR
// The main loop's call to Main::Update, the only one in the game.
constexpr uintptr_t kMainUpdateCall = 0x5B6E8F;
#endif

int TP_MAKE_THISCALL(HookVMUpdate, GameVM, float a2)
{
    if (apThis->inactive == 0)
        g_appInstance->Update();

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
#ifdef SKYRIMVR
    UnpausedMenusOnMainLoop();
    UpdateSleepWatchControls();
    RunDeferredSleepWaitAccept();
    UpdateSkillsMenuChecks();
    RunDeferredSkillsTreeUpdate();
    ReleaseClosedSkillsTrees();
    RunHeldMapHide();
    VRBodySync::AddPendingWeaponBodies();
    if (entt::locator<World>::has_value())
        World::Get().GetCharacterService().LayDownStandingBodies();
    // On the main thread between frames: the client's own update may run on another thread during a load.
    HeadPartWatch::Update();
#endif

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
        POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926, 53926);
        VMUpdate = cVMUpdate.Get();
        TP_HOOK(&VMUpdate, HookVMUpdate);

#ifdef SKYRIMVR
        // VR needs Main::Update for the unpaused Sleep/Wait OK and the party's control lock; without it a granted sleep's OK button does nothing.
        // Its call is swapped and the function left as it is: CBPC detours Main::Update, following a jump at its start and copying the bytes it finds there without relocating them.
        // With the function detoured, CBPC's copy held our hook's first call, which then landed in the heap and crashed the game at the main menu.
        auto* pMainUpdateCall = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kMainUpdateCall);
        if (PatchCheck::IsCall(pMainUpdateCall))
            TiltedPhoques::SwapCall(pMainUpdateCall, MainLoop, &HookMainLoop);
        else
            spdlog::error("Main loop: {} is not the call to Main::Update ({}), leaving it alone; a sleep's OK button does nothing", fmt::ptr(pMainUpdateCall),
                          PatchCheck::Bytes(pMainUpdateCall, 5));
#else
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564, 36564);
        MainLoop = cMainLoop.Get();
        TP_HOOK(&MainLoop, HookMainLoop);

        // An empty placeholder, and the address does not exist on VR.
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412, 40412);
        VMDestructor = cVMDestructor.Get();
        TP_HOOK(&VMDestructor, HookVMDestructor);
#endif
    });

