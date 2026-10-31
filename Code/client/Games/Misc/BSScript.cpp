#include <Misc/BSScript.h>
#include <Misc/NativeFunction.h>

#include <World.h>
#include <Events/PapyrusFunctionRegisterEvent.h>
#include <Events/ScriptDoorEvent.h>
#include <Forms/TESForm.h>
#include <Misc/GameVM.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Games/PapyrusFunctions.h>
#include <Games/Memory.h>
#include <Structs/DoorState.h>

TP_THIS_FUNCTION(TRegisterPapyrusFunction, void, BSScript::IVirtualMachine, NativeFunction*);
TP_THIS_FUNCTION(TBindEverythingToScript, void, BSScript::IVirtualMachine*);
TP_THIS_FUNCTION(TCompareVariables, int64_t, void, BSScript::Variable*, BSScript::Variable*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;
TBindEverythingToScript* RealBindEverythingToScript = nullptr;
TCompareVariables* RealCompareVariables = nullptr;

using TSetOpen = void(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, TESObjectREFR*, bool);
static TSetOpen RealSetOpen = nullptr;

// What the game's scripts reach when they call SetOpen.
// TESObjectREFR::SetOpen calls the game's own function, so a door set from another game does not come through here.
static void __fastcall HookSetOpen(BSScript::IVirtualMachine* apVirtualMachine, uint32_t aStackId, TESObjectREFR* apRef, bool aOpen)
{
    const bool cDoor = apRef && apRef->baseForm && apRef->baseForm->formType == FormType::Door;
    const uint8_t cBefore = cDoor ? static_cast<uint8_t>(apRef->GetOpenState()) : static_cast<uint8_t>(DoorState::kNone);

    RealSetOpen(apVirtualMachine, aStackId, apRef, aOpen);

    // A door already there, or on its way, did not move.
    if (DoorState::AfterActivation(cBefore) == (aOpen ? DoorState::kOpen : DoorState::kClosed) && !apRef->IsTemporary() && !IsProcessExiting())
        World::Get().GetRunner().Trigger(ScriptDoorEvent(apRef->formID, aOpen));
}

void TP_MAKE_THISCALL(HookRegisterPapyrusFunction, BSScript::IVirtualMachine, NativeFunction* apFunction)
{
    auto& runner = World::Get().GetRunner();

    PapyrusFunctionRegisterEvent event(apFunction->functionName.AsAscii(), apFunction->typeName.AsAscii(), apFunction->functionAddress);

    runner.Trigger(std::move(event));

    // Scripts call a native through this entry; PapyrusService keeps the game's own address from the event above.
    if (_stricmp(apFunction->functionName.AsAscii(), "SetOpen") == 0 && _stricmp(apFunction->typeName.AsAscii(), "ObjectReference") == 0 &&
        apFunction->functionAddress && apFunction->functionAddress != reinterpret_cast<void*>(&HookSetOpen))
    {
        RealSetOpen = reinterpret_cast<TSetOpen>(apFunction->functionAddress);
        apFunction->functionAddress = reinterpret_cast<void*>(&HookSetOpen);
    }

    TiltedPhoques::ThisCall(RealRegisterPapyrusFunction, apThis, apFunction);
}

#ifdef SKYRIMVR
TP_THIS_FUNCTION(THandleIsType, bool, BSScript::IObjectHandlePolicy, uint32_t aType, uint64_t aHandle);
static THandleIsType* RealHandleIsType = nullptr;

// The game asks a form whether it is a reference through the form's vtable, at SkyrimVR.exe+0x93CE11.
// A stale lookup (Engine Fixes' form cache) gives it a freed form or reused memory, and that call jumps into nothing.
// So a form handle whose lookup is stale is answered as no such form.
bool TP_MAKE_THISCALL(HookHandleIsType, BSScript::IObjectHandlePolicy, uint32_t aType, uint64_t aHandle)
{
    // A form's handle holds 0xFFFF above the form id; aliases and active effects have other values there.
    constexpr uint64_t cFormHandleTag = 0xFFFF;
    if ((aHandle >> 32) == cFormHandleTag && IsStaleFormId(static_cast<uint32_t>(aHandle)))
    {
        static std::atomic<uint32_t> s_stale{0};
        const uint32_t cCount = ++s_stale;
        if (cCount <= 20 || cCount % 500 == 0)
            spdlog::warn("FormGuard: script handle {:X} names a form the lookup has stale; answered as no such form ({} so far)", aHandle, cCount);
        return false;
    }

    return TiltedPhoques::ThisCall(RealHandleIsType, apThis, aType, aHandle);
}
#endif

void TP_MAKE_THISCALL(HookBindEverythingToScript, BSScript::IVirtualMachine*)
{
#ifdef SKYRIMVR
    // RegisterPapyrusFunction (104788) has no VR address; it is the VM's BindNativeMethod (vtable slot 0x18).
    // Hook it from the vtable before the game registers its natives.
    static bool s_registerHookInstalled = false;
    if (!s_registerHookInstalled && apThis && *apThis)
    {
        auto** ppVTable = *reinterpret_cast<void***>(*apThis);
        RealRegisterPapyrusFunction = static_cast<TRegisterPapyrusFunction*>(ppVTable[0x18]);
        TP_HOOK_IMMEDIATE(&RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);

        // HandleIsType is slot 1 of the handle policy (CommonLibVR, SkyrimScript::HandlePolicy).
        if (auto* pPolicy = (*apThis)->GetObjectHandlePolicy())
        {
            RealHandleIsType = static_cast<THandleIsType*>((*reinterpret_cast<void***>(pPolicy))[1]);
            TP_HOOK_IMMEDIATE(&RealHandleIsType, HookHandleIsType);
        }
        s_registerHookInstalled = true;
    }
#endif

    (*apThis)->BindNativeMethod(new BSScript::IsRemotePlayerFunc("IsRemotePlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsRemotePlayer, BSScript::Variable::kBoolean));
    (*apThis)->BindNativeMethod(new BSScript::IsPlayerFunc("IsPlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsPlayer, BSScript::Variable::kBoolean));
    (*apThis)->BindNativeMethod(new BSScript::DidLaunchSkyrimTogetherFunc("DidLaunchSkyrimTogether", "SkyrimTogetherVerifyLaunchScript", PapyrusFunctions::DidLaunchSkyrimTogether, BSScript::Variable::kBoolean));

    TiltedPhoques::ThisCall(RealBindEverythingToScript, apThis);
}

// This is a neat hack, but it has been disabled since it messes up other things like beastform.
// These kinds of issues should be solved with custom scripts now that we have SkyrimTogether.esp anyway.
int64_t TP_MAKE_THISCALL(HookCompareVariables, void, BSScript::Variable* apVar1, BSScript::Variable* apVar2)
{
    BSScript::Object* pObject1 = apVar1->GetObject();
    BSScript::Object* pObject2 = apVar2->GetObject();

    if (!pObject1 || !pObject2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    uint64_t handle1 = pObject1->GetHandle();
    uint64_t handle2 = pObject2->GetHandle();

    auto* pPolicy = GameVM::Get()->virtualMachine->GetObjectHandlePolicy();

    if (!pPolicy || !handle1 || !handle2 || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle1) || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle2) || !pPolicy->IsHandleObjectAvailable(handle1) || !pPolicy->IsHandleObjectAvailable(handle2))
    {
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
    }

    Actor* pActor1 = pPolicy->GetObjectForHandle<Actor>(handle1);
    Actor* pActor2 = pPolicy->GetObjectForHandle<Actor>(handle2);

    if (!pActor1 || !pActor2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    if (pActor1 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor2->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }
    else if (pActor2 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor1->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }

    return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
}

static TiltedPhoques::Initializer s_vmHooks(
    []()
    {
        POINTER_SKYRIMSE(TBindEverythingToScript, s_bindEverythingToScript, 55739, 55739);

        // POINTER_SKYRIMSE(TCompareVariables, s_compareVariables, 105220, 105220);

        RealBindEverythingToScript = s_bindEverythingToScript.Get();
        // RealCompareVariables = s_compareVariables.Get();

#ifndef SKYRIMVR
        // On VR this address does not exist; HookBindEverythingToScript hooks the function from the VM's vtable instead.
        POINTER_SKYRIMSE(TRegisterPapyrusFunction, s_registerPapyrusFunction, 104788, 104788);
        RealRegisterPapyrusFunction = s_registerPapyrusFunction.Get();
        TP_HOOK(&RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
#endif
        TP_HOOK(&RealBindEverythingToScript, HookBindEverythingToScript);
        // TP_HOOK(&RealCompareVariables, HookCompareVariables);
    });
