#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <CrashHandler.h>
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
// If the same part still holds it the next frame, and no live property took its address, that stack is its last release.
namespace
{
constexpr uint32_t kRefCountOffset = 0x8;         // NiRefObject::refCount
constexpr uint32_t kNameOffset = 0x10;            // NiObjectNET::name
constexpr uint32_t kChildrenOffset = 0x138;       // NiNode::children, on VR
constexpr uint32_t kShaderPropertyOffset = 0x168; // BSGeometry's shader property, on VR
constexpr uint32_t kAsGeometrySlot = 7;           // NiObject::AsGeometry, as the game's face update asks for it
constexpr size_t kMaxParts = 16;

struct Part
{
    void* pGeometry;
    void* pProperty;
    const char* pName;
};

// The head's parts as the last frame read them, for the destructor hook on any thread.
std::atomic<void*> s_watchedGeometries[kMaxParts]{};
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
    void* pGeometry;
    void* pProperty;
    uint32_t Thread;
    uint64_t Frame;
    void* Frames[24];
    USHORT Depth;
};
std::mutex s_suspectLock;
Suspect s_suspects[kMaxParts * 2]{};
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
// The crash handler is told the fault is expected, or it would report a crash first.
template <class T> bool SafeRead(const void* apAddress, T& aOut) noexcept
{
    bool read = false;
    CrashGuard::Enter();
    __try
    {
        aOut = *static_cast<const T*>(apAddress);
        read = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    CrashGuard::Leave();
    return read;
}

// A child's geometry, or null when it is none or cannot be asked.
void* GeometryOf(void* apChild) noexcept
{
    void** pVTable = nullptr;
    if (!SafeRead(apChild, pVTable) || !pVTable)
        return nullptr;
    void* pGeometry = nullptr;
    CrashGuard::Enter();
    __try
    {
        using TAsGeometry = void*(__fastcall*)(void*);
        pGeometry = reinterpret_cast<TAsGeometry>(pVTable[kAsGeometrySlot])(apChild);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        pGeometry = nullptr;
    }
    CrashGuard::Leave();
    return pGeometry;
}

std::string TextAt(const char* apText) noexcept
{
    char text[64]{};
    for (size_t i = 0; apText && i + 1 < sizeof(text) && GameStack::Readable(apText + i, 1) && apText[i]; ++i)
        text[i] = apText[i];
    return text;
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
    return TextAt(reinterpret_cast<const char*>(cModule + pFields[3] + 0x10));
}

// A top-level shader property class (".?AVBSLightingShaderProperty@@"), not one nested in such a class.
bool IsShaderPropertyClass(const std::string& acClass) noexcept
{
    constexpr std::string_view cEnd = "ShaderProperty@@";
    return acClass.size() > 4 + cEnd.size() && acClass.rfind(".?AV", 0) == 0 && acClass.compare(acClass.size() - cEnd.size(), cEnd.size(), cEnd) == 0 &&
           acClass.find('@') == acClass.size() - 2;
}

bool IsHookedVTable(void** apVTable) noexcept
{
    const size_t cHooked = s_hookedCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < cHooked; ++i)
        if (s_hooked[i].pVTable == apVTable)
            return true;
    return false;
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

    for (size_t i = 0; i < kMaxParts; ++i)
    {
        if (s_watched[i].load(std::memory_order_relaxed) != apThis)
            continue;
        std::scoped_lock lock(s_suspectLock);
        if (s_suspectCount < std::size(s_suspects))
        {
            Suspect& suspect = s_suspects[s_suspectCount++];
            suspect.pGeometry = s_watchedGeometries[i].load(std::memory_order_relaxed);
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
    if (IsHookedVTable(apVTable) || cHooked == std::size(s_hooked) || apVTable[0] == reinterpret_cast<void*>(&HookDestructor))
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
    if (!SafeRead(static_cast<uint8_t*>(apFace) + kChildrenOffset + 0x8, pChildren) || !SafeRead(static_cast<uint8_t*>(apFace) + kChildrenOffset + 0x10, capacity) ||
        !pChildren)
        return;
    for (uint16_t i = 0; i < capacity && aCount < kMaxParts; ++i)
    {
        void* pChild = nullptr;
        if (!SafeRead(&pChildren[i], pChild) || !pChild)
            continue;
        void* pGeometry = GeometryOf(pChild);
        void* pProperty = nullptr;
        const char* pName = nullptr;
        if (!pGeometry || !SafeRead(static_cast<uint8_t*>(pGeometry) + kShaderPropertyOffset, pProperty) ||
            !SafeRead(static_cast<uint8_t*>(pGeometry) + kNameOffset, pName))
            continue;
        aParts[aCount++] = {pGeometry, pProperty, pName};
    }
}

std::string Context() noexcept
{
    const bool cConnected = entt::locator<World>::has_value() && World::Get().GetTransport().IsConnected();
    return fmt::format("frame {}, connected {}, character creation {}", MainLoopFrame(), cConnected ? "yes" : "no", CharacterCreationOpen() ? "open" : "closed");
}

// Whether a live shader property now sits at this address: a new one the allocator put where the freed one was.
bool LivePropertyAt(void* apProperty) noexcept
{
    void** pVTable = nullptr;
    uint32_t refs = 0;
    return SafeRead(apProperty, pVTable) && IsHookedVTable(pVTable) && SafeRead(static_cast<uint8_t*>(apProperty) + kRefCountOffset, refs) && refs > 0;
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

    // A property destroyed while a part held it, still held by that same part now, and no live property in its place: one release too many.
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
            if (cPart.pGeometry != cSuspect.pGeometry || cPart.pProperty != cSuspect.pProperty)
                continue;
            uint32_t refs = 0;
            if (!SafeRead(static_cast<const uint8_t*>(cPart.pGeometry) + kRefCountOffset, refs) || refs == 0 || LivePropertyAt(cSuspect.pProperty))
                continue;
            if (s_toldReleases.Told(cSuspect.pProperty))
                continue;
            spdlog::error("HeadPartWatch: shader property {} was destroyed while the player's '{}' ({}, {} references) still holds it; destroyed on thread {} "
                          "(main thread {}) in frame {}; {}",
                          fmt::ptr(cSuspect.pProperty), TextAt(cPart.pName), fmt::ptr(cPart.pGeometry), refs, cSuspect.Thread, GameMainThreadId(), cSuspect.Frame,
                          Context());
            spdlog::error("HeadPartWatch:   its last release: {}", GameStack::Describe(cSuspect.Frames, cSuspect.Depth));
        }
    }

    for (size_t i = 0; i < count; ++i)
    {
        const Part& cPart = parts[i];
        if (!cPart.pProperty)
            continue;
        void** pVTable = nullptr;
        SafeRead(cPart.pProperty, pVTable);
        if (IsHookedVTable(pVTable))
            continue;

        const std::string cClass = ClassOf(pVTable);
        if (IsShaderPropertyClass(cClass))
        {
            HookClass(pVTable, cClass);
            continue;
        }
        if (s_toldMemory.Told(cPart.pProperty))
            continue;
        uint64_t words[0x68 / 8]{};
        for (size_t w = 0; w < std::size(words); ++w)
            SafeRead(static_cast<const uint64_t*>(cPart.pProperty) + w, words[w]);
        spdlog::error("HeadPartWatch: the player's '{}' ({}) holds shader property {} whose memory holds no shader property now: it begins {:X} ({}, {}), "
                      "then {:X}; at +0x60 {:X}; {}",
                      TextAt(cPart.pName), fmt::ptr(cPart.pGeometry), fmt::ptr(cPart.pProperty), words[0], GameStack::Where(pVTable),
                      cClass.empty() ? std::string("no class") : cClass, words[1], words[0x60 / 8], Context());
    }

    // The parts as now, for the next frame and for the hook.
    for (size_t i = 0; i < kMaxParts; ++i)
    {
        s_watchedGeometries[i].store(i < count ? parts[i].pGeometry : nullptr, std::memory_order_relaxed);
        s_watched[i].store(i < count ? parts[i].pProperty : nullptr, std::memory_order_relaxed);
    }
}
} // namespace HeadPartWatch
#endif
