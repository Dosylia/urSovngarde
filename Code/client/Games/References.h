#pragma once

#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>

namespace GameplayFormulas
{
// aOnOtherPlayer: a hit between players, which takes the difficulty's rule for a hit on a player, with half of what it adds.
float CalculateRealDamage(Actor* apHittee, float aDamage, bool aKillMove, bool aOnOtherPlayer = false) noexcept;
// How much longer an enemy lasts and how much harder it hits against this many of the party's players nearby, this one included.
float PartyEnemyHealth(uint32_t aPlayersNearby) noexcept;
float PartyEnemyDamage(uint32_t aPlayersNearby) noexcept;
}

namespace Settings
{
int32_t* GetDifficulty() noexcept;
float* GetGreetDistance() noexcept;
float* GetVATSSelectTargetTimeMultiplier() noexcept;
} // namespace Settings

void FadeOutGame(bool aFadingOut, bool aBlackFade, float aFadeDuration, bool aRemainVisible, float aSecondsToFade) noexcept;

struct ExActor : Actor, ActorExtension
{
};

struct ExPlayerCharacter : PlayerCharacter, ActorExtension
{
};
