#include <TiltedOnlinePCH.h>

#include <PlayerCharacter.h>
#include <Forms/BGSHeadPart.h>
#include <Forms/TESNPC.h>

#include <Games/IFormFactory.h>

#include <SaveLoad.h>
#include <Games/Overrides.h>
#include <CrashHandler.h>

// Returns the first word at this address, or 0 if reading it faults.
// The caller wraps it in CrashGuard so the crash handler treats a fault in here as expected.
static uintptr_t ReadFirstWord(const void* apAddress) noexcept
{
    __try
    {
        return *static_cast<const uintptr_t*>(apAddress);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return 0;
    }
}

// Returns this form's id, or 0 if reading it faults.
static uint32_t ReadFormId(const TESForm* apForm) noexcept
{
    __try
    {
        return apForm->formID;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return 0;
    }
}

//! Whether a pointer from a form lookup is still a real game object: readable, with its vtable inside the game image.
//! Freed memory reused for anything else fails this at once.
//! The read is guarded instead of checked with VirtualQuery, which costs about 10 ms per call on the game's large heap.
bool IsLiveGameObject(const TESForm* apForm) noexcept
{
    if (!apForm)
        return false;

    static const auto s_image = []
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + pDos->e_lfanew);
        return std::pair<uintptr_t, uintptr_t>{base, base + pNt->OptionalHeader.SizeOfImage};
    }();

    CrashGuard::Enter();
    const uintptr_t vtable = ReadFirstWord(apForm);
    CrashGuard::Leave();

    return vtable >= s_image.first && vtable < s_image.second;
}

// Whether what a lookup gave for this id is that form.
// Memory reused by another game object has a vtable in the game image too, so only its id tells them apart.
static bool IsFormWithId(const TESForm* apForm, const uint32_t aId) noexcept
{
    if (!IsLiveGameObject(apForm))
        return false;

    CrashGuard::Enter();
    const uint32_t cId = ReadFormId(apForm);
    CrashGuard::Leave();

    return cId == aId;
}

bool IsStaleFormId(const uint32_t aId) noexcept
{
    using TGetFormById = TESForm*(uint32_t);
    POINTER_SKYRIMSE(TGetFormById, getFormById, 14617, 14617);

    const TESForm* pForm = getFormById.Get()(aId);
    return pForm && !IsFormWithId(pForm, aId);
}

TESForm* TESForm::GetById(const uint32_t aId)
{
    using TGetFormById = TESForm*(uint32_t);
    POINTER_SKYRIMSE(TGetFormById, getFormById, 14617, 14617);

    TESForm* pForm = getFormById.Get()(aId);
    // The lookup can hand out a freed form: with Engine Fixes' form cache a temporary form's id stays mapped to memory
    // used for something else since, and casting that faults. So only the form asked for is returned.
    if (pForm && !IsFormWithId(pForm, aId))
    {
        static std::atomic<uint32_t> s_freed{0};
        const uint32_t cCount = ++s_freed;
        if (cCount <= 20 || cCount % 500 == 0)
            spdlog::warn("Form lookup: {:X} resolved to freed memory at {}; treated as no form ({} so far)", aId, fmt::ptr(pForm), cCount);
        return nullptr;
    }
    return pForm;
}

void TESNPC::Serialize(String* apSaveBuffer) const noexcept
{
    ScopedSaveLoadOverride saveLoadOverride;

    char buffer[1 << 15];

    BGSSaveFormBuffer saveBuffer;
    saveBuffer.buffer = buffer;
    saveBuffer.capacity = 1 << 15;
    saveBuffer.changeFlags = GetChangeFlags();

    Save(&saveBuffer);

    apSaveBuffer->assign(saveBuffer.buffer, saveBuffer.position);

    saveBuffer.buffer = nullptr;
}

void TESNPC::Deserialize(const String& acBuffer, uint32_t aChangeFlags) noexcept
{
    ScopedSaveLoadOverride saveLoadOverride;

    BGSLoadFormBuffer loadBuffer(aChangeFlags);
    loadBuffer.SetSize(acBuffer.size() & 0xFFFFFFFF);
    loadBuffer.buffer = acBuffer.data();
    loadBuffer.formId = formID;
    loadBuffer.form = this;

    Load(&loadBuffer);

    loadBuffer.buffer = nullptr;
}

void TESNPC::Initialize() noexcept
{
    auto pPlayerBaseForm = Cast<TESNPC>(PlayerCharacter::Get()->baseForm);

    // These values are all defaulted, if the other actor did not modify them they won't be loaded, therefore we need to force them before load
    attackDataForm.attackDataMap = pPlayerBaseForm->attackDataForm.attackDataMap;
    npcClass = pPlayerBaseForm->npcClass;
    combatStyle = pPlayerBaseForm->combatStyle;
    raceForm.race = pPlayerBaseForm->raceForm.race;
    defaultOutfit = pPlayerBaseForm->defaultOutfit;
    sleepOutfit = pPlayerBaseForm->sleepOutfit;
    spellList.Initialize();
    // End defaults

    flags |= 0x200000;
}

void TESForm::Save_Reversed(const uint32_t aChangeFlags, Buffer::Writer& aWriter)
{
    if (aChangeFlags & 1)
    {
        aWriter.WriteBytes(reinterpret_cast<uint8_t*>(&flags), 4);
        aWriter.WriteBytes(reinterpret_cast<uint8_t*>(&unk10), 2);
    }
}

void TESForm::SetSkipSaveFlag(bool aSet) noexcept
{
    if (aSet)
    {
        unk10 = 0xFFFF;
    }
    /*const uint32_t flag = 1 << 14;

    if (aSet)
        flags |= flag;
    else
        flags &= ~flag;*/
}

uint32_t TESForm::GetChangeFlags() const noexcept
{
    // BGSSaveLoadGame: unk330 is saveLoadChanges. VR has extra load-order tables before it (0x500).
    struct Unk
    {
#ifdef SKYRIMVR
        uint8_t unk0[0x500];
#else
        uint8_t unk0[0x330];
#endif
        void* unk330;
    };

    TP_THIS_FUNCTION(InternalGetChangeFlags, bool, void, uint32_t formId, ChangeFlags& changeFlags);

    POINTER_SKYRIMSE(InternalGetChangeFlags, internalGetChangeFlags, 35503, 34582);

    POINTER_SKYRIMSE(Unk*, s_singleton, 403330, 516851);

    const auto pUnk = *(s_singleton.Get());

    ChangeFlags changeFlags;
    const auto cResult = TiltedPhoques::ThisCall(internalGetChangeFlags, pUnk->unk330, formID, changeFlags);
    if (!cResult)
        return 0;

    return changeFlags.flags;
}

TESNPC* TESNPC::Create(const String& acBuffer, const uint32_t aChangeFlags) noexcept
{
    auto pNpc = IFormFactory::Create<TESNPC>();

    pNpc->Initialize();
    pNpc->Deserialize(acBuffer, aChangeFlags);

    // This forces facegen for some reason
    pNpc->originalRace = nullptr;

    return pNpc;
}

BGSHeadPart* TESNPC::GetHeadPart(uint32_t aType)
{
    if (headparts)
    {
        for (auto i = 0; i < headpartsCount; ++i)
        {
            if (headparts[i] && headparts[i]->type == aType)
                return headparts[i];
        }
    }

    return nullptr;
}
