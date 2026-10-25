#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"
#include <Misc/GameVM.h>
#include <Interface/Menus/SleepWaitMenu.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

struct Main;

TP_THIS_FUNCTION(TVMUpdate, int, GameVM, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

#ifdef SKYRIMVR
void RunDeferredSkillsTreeUpdate(); // Interface/Menus/SkillsMenu.cpp
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
    UpdateSleepWatchControls();
    RunDeferredSleepWaitAccept();
#endif

#ifdef SKYRIMVR
    RunDeferredSkillsTreeUpdate();
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

        // VR needs Main::Update (VRAddressOverrides) for the unpaused Sleep/Wait OK and the party's control lock;
        // without it a granted sleep's OK button does nothing.
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564, 36564);
        MainLoop = cMainLoop.Get();
        TP_HOOK(&MainLoop, HookMainLoop);

#ifndef SKYRIMVR
        // An empty placeholder, and the address does not exist on VR.
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412, 40412);
        VMDestructor = cVMDestructor.Get();
        TP_HOOK(&VMDestructor, HookVMDestructor);
#endif
    });

