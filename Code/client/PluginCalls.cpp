#include <TiltedOnlinePCH.h>

#include <PluginCalls.h>

#include <World.h>
#include <Services/CharacterService.h>
#include <Services/ObjectService.h>

#include <mutex>

// The calls another plugin can make into this mod.
// It finds them with GetProcAddress on the main module, and does without them when this mod is absent.
// These names and signatures are a promise to those plugins.
// The calls that change the game wait for the main thread, so any thread may call them.

namespace
{
std::mutex s_callsLock;
TiltedPhoques::Vector<std::function<void()>> s_calls;

bool IsConnected() noexcept
{
    return entt::locator<World>::has_value() && World::Get().GetTransport().IsConnected();
}

bool IsPartyLeader() noexcept
{
    return IsConnected() && World::Get().GetPartyService().IsLeader();
}

void AddCall(std::function<void()> aCall) noexcept
{
    std::scoped_lock lock(s_callsLock);
    s_calls.push_back(std::move(aCall));
}
} // namespace

// Not on the world's task queue: draining it holds its lock, and enabling an actor queues an event on it.
// The lock is not held here either, so a plugin the game calls back during one of these can make another.
void RunPluginCalls() noexcept
{
    TiltedPhoques::Vector<std::function<void()>> calls;
    {
        std::scoped_lock lock(s_callsLock);
        calls.swap(s_calls);
    }
    for (const auto& call : calls)
        call();
}

extern "C"
{
// A game that is not connected plays alone, and does its own switching and reviving.
__declspec(dllexport) bool urSovngarde_IsConnected() noexcept
{
    return IsConnected();
}

__declspec(dllexport) bool urSovngarde_IsPartyLeader() noexcept
{
    return IsPartyLeader();
}

// Switches a placed actor off or on in every game of the party, for players who join later too.
// True when the switch was taken, which only the party's leader can ask for.
__declspec(dllexport) bool urSovngarde_SwitchActor(uint32_t aFormId, bool aEnabled) noexcept
{
    if (!IsPartyLeader())
        return false;

    AddCall([aFormId, aEnabled] { World::Get().ctx().at<ObjectService>().SwitchActor(aFormId, aEnabled); });
    return true;
}

// Revives a dead actor in every game of the party, whichever game owns it.
// True when the revival was taken, which only the party's leader can ask for.
__declspec(dllexport) bool urSovngarde_ReviveActor(uint32_t aFormId) noexcept
{
    if (!IsPartyLeader())
        return false;

    AddCall([aFormId] { World::Get().GetCharacterService().ReviveActor(aFormId); });
    return true;
}
}
