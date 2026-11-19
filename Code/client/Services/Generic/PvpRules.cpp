#include <TiltedOnlinePCH.h>

#include <Services/PvpRules.h>
#include <Services/PartyService.h>
#include <World.h>
#include <Components.h>
#include <Actor.h>
#include <Games/ActorExtension.h>

#include <atomic>
#include <chrono>

namespace
{
#ifdef SKYRIMVR
constexpr bool kThisGameIsFlat = false;
#else
constexpr bool kThisGameIsFlat = true;
#endif

/**
 * Name a game for the log.
 *
 * @param aFlat True for flat Skyrim, false for Skyrim VR.
 * @return "flat Skyrim" or "Skyrim VR".
 */
const char* GameName(const bool aFlat) noexcept
{
    return aFlat ? "flat Skyrim" : "Skyrim VR";
}

/**
 * Check if a player plays the other game than this player: flat Skyrim against Skyrim VR.
 *
 * @param acParty The party service, which knows who plays flat Skyrim.
 * @param aPlayerId The player to check.
 * @return True when that player plays the other game.
 */
bool PlaysOtherGame(const PartyService& acParty, const uint32_t aPlayerId) noexcept
{
    return acParty.IsFlatPlayer(aPlayerId) != kThisGameIsFlat;
}

/**
 * Write in the log that no hit is allowed with a copy whose player plays the other game.
 * At most every 5 seconds, from any thread: a concentration spell asks every frame.
 *
 * @param apCopy The copy no hit is allowed with.
 */
void SayNoHitWithCopy(const Actor* apCopy) noexcept
{
    static std::atomic<int64_t> s_nextSayMs{0};
    const int64_t cNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t nextSayMs = s_nextSayMs.load();
    // Two threads may both be due: only the one that moves the next time on writes the line.
    if (cNowMs >= nextSayMs && s_nextSayMs.compare_exchange_strong(nextSayMs, cNowMs + 5000))
        spdlog::info("PvP: no hit between this player and copy {:X}: its player plays {}", apCopy->formID, GameName(!kThisGameIsFlat));
}
} // namespace

namespace PvpRules
{
/**
 * Check if this player and the player behind a copy in this game can hurt each other.
 * Safe on any thread: it reads only the server's setting and the copy's own mark.
 *
 * @param apCopy Another player's copy in this game.
 * @return True when PvP is on and the copy's player plays the same game as this player.
 */
bool IsAllowedWithCopy(Actor* apCopy) noexcept
{
    if (!World::Get().GetServerSettings().PvpEnabled)
        return false;

    const ActorExtension* pExtension = apCopy ? apCopy->GetExtension() : nullptr;
    const bool cOtherGame = pExtension && pExtension->IsOtherGame();
    if (!cOtherGame)
        return true;

    SayNoHitWithCopy(apCopy);
    return false;
}

/**
 * Check if this player and another player, named by id, can hurt each other, as when a message names its attacker.
 * Main thread only.
 *
 * @param aPlayerId The other player's id.
 * @return True when PvP is on and that player plays the same game as this player.
 */
bool IsAllowedWithPlayer(const uint32_t aPlayerId) noexcept
{
    if (!World::Get().GetServerSettings().PvpEnabled)
        return false;
    return !PlaysOtherGame(World::Get().GetPartyService(), aPlayerId);
}

/**
 * Mark each other player's copy whose player plays the other game, flat Skyrim or Skyrim VR, so IsAllowedWithCopy can tell on any thread.
 * Main thread only; run every frame, so a copy is marked before it can hit or be hit.
 *
 * @param aWorld The client's world, which lists the other players' copies.
 */
void MarkOtherGameCopies(World& aWorld) noexcept
{
    const auto& cParty = aWorld.GetPartyService();
    auto view = aWorld.view<FormIdComponent, PlayerComponent>();
    for (const auto entity : view)
    {
        auto* pCopy = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        ActorExtension* pExtension = pCopy ? pCopy->GetExtension() : nullptr;
        if (!pExtension || !pExtension->IsRemotePlayer())
            continue;

        // Only a change is written, and logged once.
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        const bool cOtherGame = PlaysOtherGame(cParty, cPlayerId);
        if (cOtherGame == pExtension->IsOtherGame())
            continue;

        pExtension->SetOtherGame(cOtherGame);
        if (cOtherGame)
            spdlog::info("PvP: player {} plays {} and this player {}; no hit between the two is sent or taken (copy {:X})", cPlayerId, GameName(!kThisGameIsFlat),
                         GameName(kThisGameIsFlat), pCopy->formID);
    }
}
} // namespace PvpRules
