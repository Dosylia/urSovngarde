#include "AIProcess.h"

#include <Misc/MiddleProcess.h>

bhkCharacterController* AIProcess::GetCharController() noexcept
{
    TP_THIS_FUNCTION(TGetCharController, bhkCharacterController*, AIProcess);

    // The engine's own function answers (VR 0x140685270, SE 0x14067bdf0, in VRAddressOverrides under AE id 39856).
    //
    // The field read below is the fallback, and a check on the offset.
    // CommonLibVR: `middleHigh ? middleHigh->charController.get() : nullptr`, middleHigh at +0x08, charController at +0x250.
    // If the two ever disagree, the offset is wrong; that is logged once.
    bhkCharacterController* pFromField = middleProcess ? middleProcess->charController : nullptr;

    POINTER_SKYRIMSE(TGetCharController, getCharController, 39856, 39856);
    if (!getCharController.Get())
        return pFromField;

    bhkCharacterController* pFromGame = TiltedPhoques::ThisCall(getCharController, this);

    static bool s_disagreed = false;
    if (!s_disagreed && pFromGame != pFromField)
    {
        s_disagreed = true;
        spdlog::warn("AIProcess::GetCharController: the engine says {} and MiddleProcess+0x250 says {}; the charController offset is wrong",
                     fmt::ptr(pFromGame), fmt::ptr(pFromField));
    }

    return pFromGame;
}

void AIProcess::KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude)
{
    TP_THIS_FUNCTION(TKnockExplosion, void, AIProcess, Actor*, const NiPoint3*, float);
    POINTER_SKYRIMSE(TKnockExplosion, knockExplosion, 39895, 38858);
    TiltedPhoques::ThisCall(knockExplosion, this, apActor, aSourceLocation, afMagnitude);
}
