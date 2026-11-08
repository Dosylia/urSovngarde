#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <Structs/ActionReplayChain.h>
#include <Structs/ActorValues.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/Factions.h>

struct AssignCharacterResponse final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kAssignCharacterResponse;

    AssignCharacterResponse()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AssignCharacterResponse& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && Cookie == achRhs.Cookie && ServerId == achRhs.ServerId && PlayerId == achRhs.PlayerId && Position == achRhs.Position && CellId == achRhs.CellId && WorldSpaceId == achRhs.WorldSpaceId && BaseId == achRhs.BaseId && AllActorValues == achRhs.AllActorValues &&
               CurrentInventory == achRhs.CurrentInventory && ActionsToReplay == achRhs.ActionsToReplay && OwnershipEpoch == achRhs.OwnershipEpoch && Owner == achRhs.Owner && IsDead == achRhs.IsDead && IsWeaponDrawn == achRhs.IsWeaponDrawn && LeveledNpcPickId == achRhs.LeveledNpcPickId && FollowerOfPlayerId == achRhs.FollowerOfPlayerId && FactionsContent == achRhs.FactionsContent &&
               RevivedSinceDeath == achRhs.RevivedSinceDeath;
    }

    uint32_t Cookie{};
    uint32_t ServerId{};
    uint32_t PlayerId{};
    Vector3_NetQuantize Position{};
    GameId CellId{};
    GameId WorldSpaceId{};
    // The owner's base form, so a client adopting a levelled reference can tell whether its own roll matches.
    GameId BaseId{};
    GameId LeveledNpcPickId{};
    ActorValues AllActorValues{};
    Inventory CurrentInventory{};
    //! For a copy made from this answer, which a copy made from a spawn has from that.
    Factions FactionsContent{};
    ActionReplayChain ActionsToReplay;
    uint32_t OwnershipEpoch{};
    //! The player whose follower this is (0: nobody's).
    uint32_t FollowerOfPlayerId{};
    bool Owner{false};
    bool IsDead{};
    bool IsWeaponDrawn{};
    //! Its owner's game revived it since its last death (ActorData::RevivedSinceDeath).
    bool RevivedSinceDeath{};
};
