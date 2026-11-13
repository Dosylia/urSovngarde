#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <CrashHandler.h>
#include <Games/Skyrim/BoneHold.h>
#include <Games/Skyrim/EyeBoneWatch.h>
#include <Games/Skyrim/GameStack.h>
#include <Games/Skyrim/Interface/UI.h>

// TEMPORARY: the game's eye tracking can find an actor's eye bone freed while it is still in the skeleton.
// A node destroyed while a live parent still lists it among its children is noted with its stack: a release one too many.
// That catches the eye bone, or any bone above it, such as the head.
// A sound release detaches a node first, and a parent's own teardown has a parent with no references left, so neither is noted.
// The destructor hook is BoneHold's; this only looks at each node it destroys.
namespace
{
constexpr uint32_t kRefCountOffset = 0x8;   // NiRefObject::refCount
constexpr uint32_t kNameOffset = 0x10;      // NiObjectNET::name
constexpr uint32_t kParentOffset = 0x30;    // NiAVObject::parent
constexpr uint32_t kChildrenOffset = 0x138; // NiNode::children, on VR
constexpr uint16_t kMaxChildren = 512;
constexpr size_t kToldInFull = 20;          // then one in every kToldEvery
constexpr size_t kToldEvery = 100;

struct Suspect
{
    void* pNode;
    const char* pName;
    void* pParent;
    const char* pParentName;
    uint32_t ParentRefs;
    uint32_t Thread;
    uint64_t Frame;
    void* Frames[24];
    USHORT Depth;
};
std::mutex s_suspectLock;
Suspect s_suspects[16]{};
size_t s_suspectCount = 0;

// A read that answers false instead of faulting.
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

std::string TextAt(const char* apText) noexcept
{
    char text[64]{};
    for (size_t i = 0; apText && i + 1 < sizeof(text) && GameStack::Readable(apText + i, 1) && apText[i]; ++i)
        text[i] = apText[i];
    return text;
}

// Whether a live parent still lists the node among its children.
bool HeldByLiveParent(void* apNode, void* apParent, uint32_t& aParentRefs) noexcept
{
    aParentRefs = 0;
    if (!SafeRead(static_cast<uint8_t*>(apParent) + kRefCountOffset, aParentRefs) || aParentRefs == 0)
        return false;
    void** pChildren = nullptr;
    uint16_t capacity = 0;
    if (!SafeRead(static_cast<uint8_t*>(apParent) + kChildrenOffset + 0x8, pChildren) || !SafeRead(static_cast<uint8_t*>(apParent) + kChildrenOffset + 0x10, capacity) ||
        !pChildren)
        return false;
    for (uint16_t i = 0; i < capacity && i < kMaxChildren; ++i)
    {
        void* pChild = nullptr;
        if (SafeRead(&pChildren[i], pChild) && pChild == apNode)
            return true;
    }
    return false;
}

// Called by BoneHold's destructor hook for each node it destroys, on any thread.
void OnDestroy(void* apThis) noexcept
{
    auto* pBytes = static_cast<uint8_t*>(apThis);
    // Most nodes go with no parent, or with a parent being torn down itself: one read each.
    void* pParent = *reinterpret_cast<void* const*>(pBytes + kParentOffset);
    uint32_t parentRefs = 0;
    if (pParent && HeldByLiveParent(apThis, pParent, parentRefs))
    {
        std::scoped_lock lock(s_suspectLock);
        Suspect& suspect = s_suspects[s_suspectCount++ % std::size(s_suspects)];
        suspect.pNode = apThis;
        suspect.pName = *reinterpret_cast<const char* const*>(pBytes + kNameOffset);
        suspect.pParent = pParent;
        suspect.pParentName = nullptr;
        SafeRead(static_cast<uint8_t*>(pParent) + kNameOffset, suspect.pParentName);
        suspect.ParentRefs = parentRefs;
        suspect.Thread = GetCurrentThreadId();
        suspect.Frame = MainLoopFrame();
        suspect.Depth = GameStack::Capture(suspect.Frames, static_cast<USHORT>(std::size(suspect.Frames)));
    }
}

std::string Describe(const Suspect& acSuspect) noexcept
{
    return fmt::format("{} ('{}') freed while its parent {} ('{}', {} refs) still held it, on thread {} (main thread {}) in frame {}", fmt::ptr(acSuspect.pNode),
                       TextAt(acSuspect.pName), fmt::ptr(acSuspect.pParent), TextAt(acSuspect.pParentName), acSuspect.ParentRefs, acSuspect.Thread, GameMainThreadId(),
                       acSuspect.Frame);
}

} // namespace

namespace EyeBoneWatch
{
void Update() noexcept
{
    static bool s_observing = false;
    if (!s_observing)
    {
        BoneHold::SetDestroyObserver(&OnDestroy);
        s_observing = true;
    }
    static size_t s_told = 0;
    Suspect suspects[std::size(s_suspects)]{};
    size_t first = 0;
    size_t last = 0;
    {
        std::scoped_lock lock(s_suspectLock);
        if (s_told == s_suspectCount)
            return;
        first = std::max(s_told, s_suspectCount > std::size(s_suspects) ? s_suspectCount - std::size(s_suspects) : size_t{0});
        last = s_suspectCount;
        for (size_t i = first; i < last; ++i)
            suspects[i - first] = s_suspects[i % std::size(s_suspects)];
        s_told = last;
    }
    for (size_t i = first; i < last; ++i)
    {
        if (i >= kToldInFull && (i + 1) % kToldEvery != 0)
            continue;
        const Suspect& cSuspect = suspects[i - first];
        spdlog::error("EyeBoneWatch: node {} ({} so far); the stack of that release:\n{}", Describe(cSuspect), i + 1, GameStack::Describe(cSuspect.Frames, cSuspect.Depth));
    }
}

void Report(const uint64_t* apRegisters, size_t aCount) noexcept
{
    std::unique_lock lock(s_suspectLock, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    if (s_suspectCount == 0)
    {
        spdlog::error("EyeBoneWatch: no node was freed while its parent held it this session.");
        return;
    }
    const size_t cKept = std::min(s_suspectCount, std::size(s_suspects));
    spdlog::error("EyeBoneWatch: {} node(s) freed while their parent held them this session; the last {}, newest first:", s_suspectCount, cKept);
    for (size_t n = 0; n < cKept; ++n)
    {
        const Suspect& cSuspect = s_suspects[(s_suspectCount - 1 - n) % std::size(s_suspects)];
        bool inRegister = false;
        for (size_t r = 0; r < aCount; ++r)
            inRegister = inRegister || apRegisters[r] == reinterpret_cast<uint64_t>(cSuspect.pNode);
        spdlog::error("    {}{}\n{}", Describe(cSuspect), inRegister ? "   <<<< IN A CRASHING REGISTER" : "", GameStack::Describe(cSuspect.Frames, cSuspect.Depth));
    }
}
} // namespace EyeBoneWatch
#endif
