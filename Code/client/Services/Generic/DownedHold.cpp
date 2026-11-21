#include <TiltedOnlinePCH.h>

#include <Services/Generic/DownedHold.h>

#include <Actor.h>
#include <PlayerCharacter.h>

#include <atomic>

namespace
{
// The game's life states, as Actor::SetLifeState takes them.
constexpr uint32_t kAlive = 0;

std::atomic<bool> s_holds{false};
// Whether this down already logged a refused stand-up, so a game that tries every frame logs once.
std::atomic<bool> s_refusalLoggedThisDown{false};

TP_THIS_FUNCTION(TSetLifeState, void, Actor, uint32_t aState);
TSetLifeState* RealSetLifeState = nullptr;

/**
 * Check whether a life-state change is the game standing up this game's downed player while its down holds.
 *
 * @param apActor The actor whose life state changes.
 * @param aState The life state it is given.
 * @return True when the change is to alive, from a bleedout, for this game's player, while its down holds.
 */
bool IsHeldDownStandUp(const Actor* apActor, const uint32_t aState) noexcept
{
    return aState == kAlive && s_holds.load() && apActor == PlayerCharacter::Get() && apActor->actorState.IsBleedingOut();
}

/**
 * The game's Actor::SetLifeState, refusing only the stand-up of this game's player while its down holds.
 *
 * @param apThis The actor whose life state changes.
 * @param aState The life state it is given.
 */
void TP_MAKE_THISCALL(HookSetLifeState, Actor, uint32_t aState)
{
    if (IsHeldDownStandUp(apThis, aState))
    {
        if (!s_refusalLoggedThisDown.exchange(true))
            spdlog::info("DownedHold: the game would stand the downed player up (its health is above zero); kept down, as only a revive ends a down");
        return;
    }
    TiltedPhoques::ThisCall(RealSetLifeState, apThis, aState);
}
} // namespace

namespace DownedHold
{
/**
 * Say whether the player's down holds now.
 * PlayerService sets it while the player is down and waiting, and clears it the moment a revive, a respawn or a loaded save ends the down.
 *
 * @param aHolds True while only a revive, giving up, the end of the wait or a fallen party may end the down.
 */
void Set(const bool aHolds) noexcept
{
    const bool cHeldBefore = s_holds.exchange(aHolds);
    if (aHolds && !cHeldBefore)
        s_refusalLoggedThisDown = false;
}
} // namespace DownedHold

// Actor::SetLifeState: SE 36604 for VR, Anniversary Edition 37612 on flat.
static TiltedPhoques::Initializer s_downedHoldHook(
    []()
    {
        POINTER_SKYRIMSE(TSetLifeState, s_setLifeState, 37612, 36604);
        RealSetLifeState = s_setLifeState.Get();
        if (!RealSetLifeState)
        {
            spdlog::error("DownedHold: Actor::SetLifeState did not resolve; a heal can end a down");
            return;
        }
        TP_HOOK(&RealSetLifeState, HookSetLifeState);
    });
