#include <TiltedOnlinePCH.h>

#include <Copies/PlayerLook.h>

#include <World.h>
#include <Utils.h>
#include <PlayerCharacter.h>
#include <Forms/TESNPC.h>
#include <Services/TransportService.h>
#include <Messages/RequestRespawn.h>

namespace PlayerLook
{
Tints FaceTints() noexcept
{
    Tints result;
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return result;

    const auto& tints = pPlayer->GetTints();
    auto& entries = result.Entries;
    entries.resize(tints.length);
    for (auto i = 0u; i < tints.length; ++i)
    {
        entries[i].Alpha = tints[i]->alpha;
        entries[i].Color = tints[i]->color;
        entries[i].Type = tints[i]->type;
        if (tints[i]->texture)
            entries[i].Name = tints[i]->texture->name.AsAscii();
    }
    return result;
}

bool SendRenamed(World& aWorld, TransportService& aTransport) noexcept
{
    const auto cEntity = Utils::FindByFormId(aWorld.view<FormIdComponent>(), 0x14);
    const auto cServerId = cEntity ? Utils::GetServerId(*cEntity) : std::nullopt;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESNPC* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
    if (!cServerId || !pNpc)
        return false;

    // As a player's copy is first made: the name (0x20) travels when the game marked it, as character creation does.
    // Marking it here would write a script's rename into the next save.
    pNpc->MarkChanged(0x2000800);

    RequestRespawn request;
    request.ActorId = *cServerId;
    pNpc->Serialize(&request.AppearanceBuffer);
    request.ChangeFlags = pNpc->GetChangeFlags();
    request.FaceTints = FaceTints();
    request.Username = pNpc->fullName.value.AsAscii();
    aTransport.Send(request);
    return true;
}
} // namespace PlayerLook
