#include "MenuTopicManager.h"

#include <Events/PlayerDialogueEvent.h>
#include <TESObjectREFR.h>

TP_THIS_FUNCTION(TPlayDialogueOption, bool, MenuTopicManager, int32_t aIndex);
static TPlayDialogueOption* RealPlayDialogueOption = nullptr;

MenuTopicManager* MenuTopicManager::Get() noexcept
{
    POINTER_SKYRIMSE(MenuTopicManager*, s_singleton, 401099, 514959);
    return *s_singleton.Get();
}

bool MenuTopicManager::IsPlayerDialogueSpeaker(const TESObjectREFR* apSpeaker) noexcept
{
    if (!apSpeaker)
        return false;

    const auto* pManager = Get();
    // Not lastSpeaker: it outlives the menu and would catch later ambient speech of a remote NPC.
    return pManager && pManager->IsCurrentSpeaker(apSpeaker->GetHandle());
}

bool TP_MAKE_THISCALL(HookPlayDialogueOption, MenuTopicManager, int32_t aIndex)
{
    if (apThis->pOptions)
    {
        int i = 0;
        const char* pText = nullptr;
        for (auto option : *apThis->pOptions)
        {
            if (i == aIndex)
            {
                pText = option->text;
                break;
            }
            i++;
        }

        if (pText != nullptr)
            World::Get().GetRunner().Trigger(PlayerDialogueEvent(pText));
    }

    return TiltedPhoques::ThisCall(RealPlayDialogueOption, apThis, aIndex);
}

static TiltedPhoques::Initializer s_menuTopicHooks(
    []()
    {
        POINTER_SKYRIMSE(TPlayDialogueOption, s_playDialogueOption, 35269, 35269);

        RealPlayDialogueOption = s_playDialogueOption.Get();

        TP_HOOK(&RealPlayDialogueOption, HookPlayDialogueOption);
    });
