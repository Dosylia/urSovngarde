#pragma once

#include <NetImmerse/ImageSpaceModifierInstance.h>

struct TESImageSpaceModifier;

struct ImageSpaceModifierInstanceForm : public ImageSpaceModifierInstance
{
    // CommonLibSSE ImageSpaceModifierInstanceForm::Trigger(mod, strength, target).
    static ImageSpaceModifierInstanceForm* Trigger(TESImageSpaceModifier* apMod, float aStrength = 1.f, void* apTarget = nullptr)
    {
        using TTrigger = ImageSpaceModifierInstanceForm*(TESImageSpaceModifier*, float, void*);
        POINTER_SKYRIMSE(TTrigger, s_trigger, 18587, 18185);
        return s_trigger.Get()(apMod, aStrength, apTarget);
    }

    static void Stop(TESImageSpaceModifier* apMod)
    {
        using TClearImageSpaceModifier = void(TESImageSpaceModifier*);
        POINTER_SKYRIMSE(TClearImageSpaceModifier, s_clearImageSpaceModifier, 18573, 18188);
        return s_clearImageSpaceModifier.Get()(apMod);
    }

    TESImageSpaceModifier* pMod;
    uint64_t unk30;
    float unk38;
    uint32_t unk3C;
    uint64_t unk40;
    uint32_t unk48;
};
