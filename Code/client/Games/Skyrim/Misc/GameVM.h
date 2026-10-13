#pragma once

#include <Misc/BSScript.h>

// VR uses the SE layout; AE moved virtualMachine to 0x210 and inactive to 0x690 (a dword).
// HookVMUpdate tests inactive before running the mod's update.
struct SkyrimVM
{
    virtual ~SkyrimVM();

    static SkyrimVM* Get();

#ifdef SKYRIMVR
    uint8_t pad8[0x200 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad208[0x680 - 0x208];
    uint8_t inactive;
#else
    uint8_t pad8[0x210 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
    uint8_t pad218[0x690 - 0x218];
    int32_t inactive;
#endif
};

#ifdef SKYRIMVR
static_assert(offsetof(SkyrimVM, virtualMachine) == 0x200);
static_assert(offsetof(SkyrimVM, inactive) == 0x680);
#else
static_assert(offsetof(SkyrimVM, virtualMachine) == 0x210);
static_assert(offsetof(SkyrimVM, inactive) == 0x690);
#endif

using GameVM = SkyrimVM;
