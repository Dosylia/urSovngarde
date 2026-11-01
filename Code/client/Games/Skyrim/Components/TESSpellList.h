#pragma once

#include <Components/BaseFormComponent.h>

struct SpellItem;
struct TESForm;
struct TESShout;

struct TESSpellList : BaseFormComponent
{
    struct Lists
    {
        SpellItem** spells{nullptr};
        TESForm** unk4{nullptr};
        TESShout** shouts{nullptr};
        uint32_t spellCount{0};
        uint32_t unk4Count{0};
        uint32_t shoultCount{0};
    };

    Lists* lists;

    void Initialize();
    //! The game leaves a null for each spell or shout that does not resolve here, and its combat code reads shouts with no null check.
    uint32_t RemoveNullEntries() noexcept;
};

static_assert(offsetof(TESSpellList::Lists, shouts) == 0x10);
static_assert(offsetof(TESSpellList::Lists, shoultCount) == 0x20);
static_assert(sizeof(TESSpellList::Lists) == 0x28);
