#pragma once

#include <Structs/ActionEvent.h>

struct ActorExtension
{
    enum
    {
        kRemote = 1 << 0,
        kPlayer = 1 << 1,
        kFollowerCopy = 1 << 2,
        kHiddenHere = 1 << 3,
        kOtherGame = 1 << 4,
    };

    enum class ReconciliationStage
    {
        None,
        WaitingForDisable,
        WaitingFor3D
    };

    bool IsRemote() const noexcept;
    bool IsLocal() const noexcept;
    bool IsPlayer() const noexcept;
    bool IsRemotePlayer() const noexcept;
    bool IsLocalPlayer() const noexcept;
    //! A temporary actor showing another player's follower.
    bool IsFollowerCopy() const noexcept;
    //! A reference of this game's own disabled while a copy stands in for it; a save still has it enabled.
    bool IsHiddenHere() const noexcept;
    //! Another player's copy whose player plays the other game: flat Skyrim against Skyrim VR (PvpRules).
    bool IsOtherGame() const noexcept;
    void SetRemote(bool aSet) noexcept;
    void SetPlayer(bool aSet) noexcept;
    void SetFollowerCopy(bool aSet) noexcept;
    void SetHiddenHere(bool aSet) noexcept;
    void SetOtherGame(bool aSet) noexcept;

    ActionEvent LatestAnimation{};
    size_t GraphDescriptorHash = 0;
    // The behavior project GraphDescriptorHash was taken from; the hash is taken again when it changes.
    uint64_t GraphDescriptorProjectKey = 0;

    //! Its owner's game, this one or another, revived it since its last death, as this game last heard.
    //! An announcement from here carries it (CharacterService::BuildActorData).
    bool RevivedSinceDeath = false;
    //! A copy of another player's summon, marked here as commanded by this game's player so it turns on no one.
    //! That mark is ours, not a spell's, so it does not mean a spell is raising the body (Actor::IsBeingRaised).
    bool CommandedAsSummonCopy = false;

    // TODO: atomic? bool instead? maybe simplify to `IsReenabling()` ?
    // Protects discovery while rebuilding a leveled NPC.
    ReconciliationStage Reconciliation{ReconciliationStage::None};

private:
    uint32_t onlineFlags{0};
};
