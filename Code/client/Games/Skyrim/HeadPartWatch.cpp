#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/GameStack.h>
#include <Games/Skyrim/HeadPartWatch.h>
#include <Games/Skyrim/Interface/UI.h>
#include <NetImmerse/BSFaceGenNiNode.h>
#include <PlayerCharacter.h>
#include <World.h>

#include <cstring>

// TEMPORARY: character creation crashed three times on a part of the player's head whose shader property was freed memory.
// Each frame this reads the head's parts, and logs a part whose shader property no longer holds one.
// The destructor of each shader property class seen on the head is hooked, and a property destroyed while a part held it is noted with its stack.
// If the part still holds it the next frame, its last release was one too many, and that stack is the culprit.
namespace
{
constexpr uint32_t kRefCountOffset = 0x8;         // NiRefObject::refCount
constexpr uint32_t kNameOffset = 0x10;            // NiObjectNET::name
constexpr uint32_t kChildrenOffset = 0x138;       // NiNode::children, on VR
constexpr uint32_t kShaderPropertyOffset = 0x168; // BSGeometry's shader property, on VR
constexpr uint32_t kAsGeometrySlot = 7;           // NiAVObject's vtable slot the game's face update asks for the geometry
constexpr size_t kMaxParts = 16;

struct Part
{
    void* pGeometry;
    void* pProperty;
    char Name[48];
};

// The head's properties as the main thread last read them, for the destructor hook on any thread.
std::atomic<void*> s_watched[kMaxParts]{};

using TDestructor = void*(void*, uint32_t);
struct Hooked
{
    void** pVTable;
    TDestructor* pReal;
};
Hooked s_hooked[4]{};
std::atomic<size_t> s_hookedCount{0};

struct Suspect
{
    void* pProperty;
    uint32_t Thread;
    uint64_t Frame;
    void* Frames[24];
    USHORT Depth;
};
std::mutex s_suspectLock;
Suspect s_suspects[8]{};
size_t s_suspectCount = 0;

// Properties already told, so each is logged once by each check.
struct ToldList
{
    void* Entries[64]{};
    size_t Next = 0;

    bool Told(void* apProperty) noexcept
    {
        for (void* pTold : Entries)
            if (pTold == apProperty)
                return true;
        Entries[Next++ % std::size(Entries)] = apProperty;
        return false;
    }
};
ToldList s_toldReleases;
ToldList s_toldMemory;

// A read that answers false instead of faulting, cheap enough for every part every frame.
template <class T> bool SafeRead(const void* apAddress, T& aOut) noexcept
{
    __try
    {
        aOut = *static_cast<const T*>(apAddress);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The class of an object from MSVC's type information before its vtable, in the game or in any plugin.
// Read only, so it is safe on freed memory that another object now holds.
std::string ClassOf(const void* apVTable) noexcept
{
    const void* pLocator = nullptr;
    if (!apVTable || !SafeRead(static_cast<const void* const*>(apVTable) - 1, pLocator) || !GameStack::Readable(pLocator, 0x18))
        return {};
    const auto* pFields = static_cast<const uint32_t*>(pLocator);
    // x64 locators hold their own offset from the module, so the module is found without knowing which one it is.
    if (pFields[0] != 1)
        return {};
    const uintptr_t cModule = reinterpret_cast<uintptr_t>(pLocator) - pFields[5];
    const char* pName = reinterpret_cast<const char*>(cModule + pFields[3] + 0x10);
    if (!GameStack::Readable(pName, 4))
        return {};
    char name[96]{};
    for (size_t i = 0; i + 1 < sizeof(name) && GameStack::Readable(pName + i, 1) && pName[i]; ++i)
        name[i] = pName[i];
    return name;
}

void* HookDestructor(void* apThis, uint32_t aFlags)
{
    void** pVTable = *static_cast<void***>(apThis);
    TDestructor* pReal = nullptr;
    const size_t cHooked = s_hookedCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < cHooked; ++i)
        if (s_hooked[i].pVTable == pVTable)
            pReal = s_hooked[i].pReal;
    // Only a recorded vtable gets this hook, so this does not happen; the object is then left alive rather than called through nothing.
    if (!pReal)
        return apThis;

    for (const auto& cWatched : s_watched)
    {
        if (cWatched.load(std::memory_order_relaxed) != apThis)
            continue;
        std::scoped_lock lock(s_suspectLock);
        if (s_suspectCount < std::size(s_suspects))
        {
            Suspect& suspect = s_suspects[s_suspectCount++];
            suspect.pProperty = apThis;
            suspect.Thread = GetCurrentThreadId();
            suspect.Frame = MainLoopFrame();
            suspect.Depth = GameStack::Capture(suspect.Frames, static_cast<USHORT>(std::size(suspect.Frames)));
        }
        break;
    }
    return pReal(apThis, aFlags);
}

void HookClass(void** apVTable, const std::string& acClass) noexcept
{
    const size_t cHooked = s_hookedCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < cHooked; ++i)
        if (s_hooked[i].pVTable == apVTable)
            return;
    if (cHooked == std::size(s_hooked) || apVTable[0] == reinterpret_cast<void*>(&HookDestructor))
        return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&apVTable[0], sizeof(void*), PAGE_READWRITE, &oldProtect))
        return;
    s_hooked[cHooked] = {apVTable, reinterpret_cast<TDestructor*>(apVTable[0])};
    s_hookedCount.store(cHooked + 1, std::memory_order_release);
    apVTable[0] = reinterpret_cast<void*>(&HookDestructor);
    VirtualProtect(&apVTable[0], sizeof(void*), oldProtect, &oldProtect);
    spdlog::info("HeadPartWatch: the destruction of {} ({}) is watched", acClass, GameStack::Where(apVTable));
}

bool CharacterCreationOpen() noexcept
{
    static const BSFixedString s_menu("RaceSex Menu");
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(s_menu);
}

void ReadParts(void* apFace, Part (&aParts)[kMaxParts], size_t& aCount) noexcept
{
    aCount = 0;
    void** pChildren = nullptr;
    uint16_t capacity = 0;
    if (!SafeRead(static_cast<uint8_t*>(apFace) + kChildrenOffset + 0x8, pChildren) || !SafeRead(static_cast<uint8_t*>(apFace) + kChildrenOffset + 0x10, capacity) || !pChildren)
        return;
    for (uint16_t i = 0; i < capacity && aCount < kMaxParts; ++i)
    {
        void* pChild = nullptr;
        void** pChildVTable = nullptr;
        if (!SafeRead(&pChildren[i], pChild) || !pChild || !SafeRead(pChild, pChildVTable) || !pChildVTable)
            continue;
        using TAsGeometry = void*(__fastcall*)(void*);
        void* pGeometry = reinterpret_cast<TAsGeometry>(pChildVTable[kAsGeometrySlot])(pChild);
        void* pProperty = nullptr;
        if (!pGeometry || !SafeRead(static_cast<uint8_t*>(pGeometry) + kShaderPropertyOffset, pProperty))
            continue;
        Part& part = aParts[aCount++];
        part.pGeometry = pGeometry;
        part.pProperty = pProperty;
        std::memset(part.Name, 0, sizeof(part.Name));
        const char* pName = *reinterpret_cast<const char**>(static_cast<uint8_t*>(pGeometry) + kNameOffset);
        for (size_t c = 0; pName && c + 1 < sizeof(part.Name) && GameStack::Readable(pName + c, 1) && pName[c]; ++c)
            part.Name[c] = pName[c];
    }
}

std::string Context() noexcept
{
    return fmt::format("frame {}, connected {}, character creation {}", MainLoopFrame(), World::Get().GetTransport().IsConnected() ? "yes" : "no",
                       CharacterCreationOpen() ? "open" : "closed");
}
} // namespace

namespace HeadPartWatch
{
void Update() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    void* pFace = pPlayer ? static_cast<void*>(pPlayer->GetFaceGenNiNode()) : nullptr;

    Part parts[kMaxParts]{};
    size_t count = 0;
    if (pFace)
        ReadParts(pFace, parts, count);

    // Character creation opened or closed: how many parts are watched, and how often the head was rebuilt meanwhile.
    static bool s_creationOpen = false;
    static uint32_t s_rebuilds = 0;
    static void* s_lastGeometries[kMaxParts]{};
    const bool cCreationOpen = CharacterCreationOpen();
    if (cCreationOpen != s_creationOpen)
    {
        s_creationOpen = cCreationOpen;
        spdlog::info("HeadPartWatch: character creation {}; {} head parts watched, {} shader property classes hooked{}; {}", cCreationOpen ? "opened" : "closed", count,
                     s_hookedCount.load(), cCreationOpen ? std::string() : fmt::format(", the head rebuilt {} times while it was open", s_rebuilds), Context());
        s_rebuilds = 0;
    }
    void* geometries[kMaxParts]{};
    for (size_t i = 0; i < count; ++i)
        geometries[i] = parts[i].pGeometry;
    if (!std::equal(std::begin(geometries), std::end(geometries), std::begin(s_lastGeometries)))
    {
        std::copy(std::begin(geometries), std::end(geometries), std::begin(s_lastGeometries));
        if (cCreationOpen)
            ++s_rebuilds;
    }

    // A property destroyed while a part held it, and still held by that part now: one release too many.
    Suspect suspects[std::size(s_suspects)]{};
    size_t suspectCount = 0;
    {
        std::scoped_lock lock(s_suspectLock);
        suspectCount = s_suspectCount;
        std::copy_n(s_suspects, suspectCount, suspects);
        s_suspectCount = 0;
    }
    for (size_t s = 0; s < suspectCount; ++s)
    {
        const Suspect& cSuspect = suspects[s];
        for (size_t i = 0; i < count; ++i)
        {
            const Part& cPart = parts[i];
            if (cPart.pProperty != cSuspect.pProperty || !GameStack::Readable(cPart.pGeometry, kRefCountOffset + 4))
                continue;
            const uint32_t cRefs = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(cPart.pGeometry) + kRefCountOffset);
            if (cRefs == 0 || s_toldReleases.Told(cSuspect.pProperty))
                continue;
            spdlog::error("HeadPartWatch: shader property {} was destroyed while the player's '{}' ({}, {} references) still holds it; destroyed on thread {} "
                          "(main thread {}) in frame {}; {}",
                          fmt::ptr(cSuspect.pProperty), cPart.Name, fmt::ptr(cPart.pGeometry), cRefs, cSuspect.Thread, GameMainThreadId(), cSuspect.Frame, Context());
            spdlog::error("HeadPartWatch:   the release that destroyed it: {}", GameStack::Describe(cSuspect.Frames, cSuspect.Depth));
        }
    }

    for (size_t i = 0; i < count; ++i)
    {
        Part& part = parts[i];
        if (!part.pProperty)
            continue;
        void** pVTable = nullptr;
        SafeRead(part.pProperty, pVTable);
        bool cHookedClass = false;
        const size_t cHooked = s_hookedCount.load(std::memory_order_acquire);
        for (size_t h = 0; h < cHooked; ++h)
            cHookedClass |= s_hooked[h].pVTable == pVTable;
        if (cHookedClass)
            continue;

        const std::string cClass = ClassOf(pVTable);
        if (cClass.find("ShaderProperty@@") != std::string::npos)
        {
            HookClass(pVTable, cClass);
            continue;
        }
        if (s_toldMemory.Told(part.pProperty))
            continue;
        const uint64_t* pWords = GameStack::Readable(part.pProperty, 0x70) ? static_cast<const uint64_t*>(part.pProperty) : nullptr;
        spdlog::error("HeadPartWatch: the player's '{}' ({}) holds shader property {} whose memory holds no shader property now: it begins {:X} ({}, {}), "
                      "then {:X}; at +0x60 {:X}; {}",
                      part.Name, fmt::ptr(part.pGeometry), fmt::ptr(part.pProperty), pWords ? pWords[0] : 0, GameStack::Where(pVTable),
                      cClass.empty() ? std::string("no class") : cClass, pWords ? pWords[1] : 0, pWords ? pWords[0x60 / 8] : 0, Context());
    }

    // The parts as now, for the next frame and for the hook.
    for (size_t i = 0; i < kMaxParts; ++i)
        s_watched[i].store(i < count ? parts[i].pProperty : nullptr, std::memory_order_relaxed);
}
} // namespace HeadPartWatch
#endif
