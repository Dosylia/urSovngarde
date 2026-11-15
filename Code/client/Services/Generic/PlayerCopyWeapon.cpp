#include <TiltedOnlinePCH.h>

#include <Services/Generic/PlayerCopyWeapon.h>

#include <World.h>
#include <Components.h>
#include <Services/CharacterService.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Skyrim/Interface/UI.h>

namespace
{
struct Wanted
{
    bool Drawn{};
    // When the copy was first seen showing the other state, while this game ran; empty while it matches.
    std::chrono::steady_clock::time_point MismatchSince{};
    // Draws or sheathes asked for this state: a copy the game will not change (a kill move, a beast form) is not asked forever.
    uint32_t Tries{};
    // The owner drew while this game was paused, so the draw's animation was refused here for sure.
    // The owner's later actions can still set the copy's state to drawn with the sword left in its sheath, so it is drawn once this game runs, whatever its state says.
    bool DrewWhilePaused{};
    // Since when this game has run again, for that draw.
    std::chrono::steady_clock::time_point RunningSince{};
};

bool IsPaused() noexcept
{
    const UI* pUi = UI::Get();
    return pUi && pUi->numPausesGame > 0;
}
// By server id.
TiltedPhoques::Map<uint32_t, Wanted> s_wanted;
// The last frame this game was paused.
// The owner sends its state on a half-second poll once the draw is done, so the draw's animation came up to about 1.5 s before the state.
std::chrono::steady_clock::time_point s_lastPaused{};
} // namespace

namespace PlayerCopyWeapon
{
void OnNotify(const NotifyDrawWeapon& acMessage) noexcept
{
    const bool cPausedLately = IsPaused() || std::chrono::steady_clock::now() - s_lastPaused < std::chrono::seconds(2);
    s_wanted[acMessage.Id] = Wanted{acMessage.IsWeaponDrawn, {}, 0, acMessage.IsWeaponDrawn && cPausedLately, {}};
}

void OnSpawn(const CharacterSpawnRequest& acMessage) noexcept
{
    s_wanted.erase(acMessage.ServerId);
}

void Run(World& aWorld, const bool aConnected) noexcept
{
    // Server ids are given out again after a reconnection.
    if (!aConnected)
    {
        s_wanted.clear();
        return;
    }

    static std::chrono::steady_clock::time_point s_nextRun{};
    const auto cNow = std::chrono::steady_clock::now();
    if (IsPaused())
        s_lastPaused = cNow;
    if (s_wanted.empty() || cNow < s_nextRun)
        return;
    s_nextRun = cNow + std::chrono::milliseconds(500);

    // A paused game refuses the draw as it refused the replay: the wait starts again once it runs.
    if (IsPaused())
    {
        for (auto it = s_wanted.begin(); it != s_wanted.end(); ++it)
        {
            it.value().MismatchSince = {};
            it.value().RunningSince = {};
        }
        return;
    }

    auto& characterService = aWorld.GetCharacterService();
    auto view = aWorld.view<RemoteComponent, FormIdComponent>();
    for (auto it = s_wanted.begin(); it != s_wanted.end();)
    {
        const uint32_t cServerId = it->first;
        const auto cEntity = std::find_if(view.begin(), view.end(), [&view, cServerId](const entt::entity aEntity) { return view.get<RemoteComponent>(aEntity).Id == cServerId; });
        // No copy here (out of range): one made later takes its owner's state from the server.
        if (cEntity == view.end())
        {
            it = s_wanted.erase(it);
            continue;
        }

        Wanted& wanted = it.value();
        const uint32_t cFormId = view.get<FormIdComponent>(*cEntity).Id;
        Actor* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        const auto cState = pActor ? pActor->actorState.GetWeaponState() : ActorState::WeaponState::Sheathed;
        // Only a settled copy is judged: a draw or sheathe under way, or one queued, finishes first.
        const bool cSettled = cState == ActorState::WeaponState::Sheathed || cState == ActorState::WeaponState::Drawn;
        // A copy down or dying does not draw; its weapon comes with getting up, or not at all.
        const bool cDown = pActor && (pActor->actorState.IsDeadOrDying() || pActor->actorState.IsBleedingOut() || pActor->actorState.IsKnockedDown());

        // A copy down, or one with a draw already queued (it arrived, or changed hands), gets its weapon that way.
        if (wanted.DrewWhilePaused && (cDown || characterService.IsWeaponDrawQueued(cFormId)))
            wanted.DrewWhilePaused = false;

        // A draw made while this game was paused: drawn a second after it runs again.
        if (wanted.DrewWhilePaused && pActor && pActor->GetNiNode() && pActor->GetExtension()->IsRemotePlayer() && !cDown && wanted.Tries < 2 &&
            !characterService.IsWeaponDrawQueued(cFormId))
        {
            if (wanted.RunningSince == std::chrono::steady_clock::time_point{})
                wanted.RunningSince = cNow;
            if (cNow - wanted.RunningSince >= std::chrono::seconds(1))
            {
                ++wanted.Tries;
                wanted.DrewWhilePaused = false;
                spdlog::info("PlayerCopyWeapon: copy {:X}'s owner drew while this game was paused, its state here {}; drawing it (try {} of 2)", cFormId,
                             ActorState::WeaponStateName(cState), wanted.Tries);
                characterService.QueueWeaponDraw(cFormId, true, true);
            }
            ++it;
            continue;
        }

        if (!pActor || !pActor->GetNiNode() || !pActor->GetExtension()->IsRemotePlayer() || !cSettled || cDown || wanted.Tries >= 2 ||
            characterService.IsWeaponDrawQueued(cFormId) || (cState == ActorState::WeaponState::Drawn) == wanted.Drawn)
        {
            wanted.MismatchSince = {};
            ++it;
            continue;
        }

        if (wanted.MismatchSince == std::chrono::steady_clock::time_point{})
        {
            wanted.MismatchSince = cNow;
            ++it;
            continue;
        }
        if (cNow - wanted.MismatchSince < std::chrono::milliseconds(1500))
        {
            ++it;
            continue;
        }

        ++wanted.Tries;
        spdlog::info("PlayerCopyWeapon: copy {:X} showed its weapon {} for {} ms while this game ran, and its owner has it {}; {} it (try {} of 2)", cFormId,
                     wanted.Drawn ? "sheathed" : "drawn", std::chrono::duration_cast<std::chrono::milliseconds>(cNow - wanted.MismatchSince).count(), wanted.Drawn ? "drawn" : "sheathed",
                     wanted.Drawn ? "drawing" : "sheathing", wanted.Tries);
        characterService.QueueWeaponDraw(cFormId, wanted.Drawn);
        wanted.MismatchSince = {};
        ++it;
    }
}
} // namespace PlayerCopyWeapon
