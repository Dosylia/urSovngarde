#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

//! The blows that landed on this player lately, to tell how it went down (DeathSentence).
namespace DeathCause
{
struct Hit
{
    std::chrono::steady_clock::time_point At{};
    //! The actor that landed it in this game; 0 for another player's hit sent over the network.
    uint32_t AttackerFormId = 0;
    //! Another player's hit sent over the network (the defender's rule); 0 for one this game saw.
    uint32_t AttackerPlayerId = 0;
    //! The weapon, spell or shout; 0 when the game gave none.
    uint32_t SourceFormId = 0;
    uint32_t ProjectileFormId = 0;
    //! The game's hit flags: 1 power attack, 2 sneak attack, 4 bash, 8 blocked.
    uint8_t Flags = 0;
};

constexpr uint8_t kBashFlag = 4;

//! Starts listening to the game's hits on the player; once is enough.
void Start() noexcept;

//! Another player's hit on this player, applied once the defender's rule let it land.
void RecordPlayerHit(uint32_t aAttackerPlayerId) noexcept;

//! The last blow that landed between those two moments, if any.
std::optional<Hit> LastBetween(std::chrono::steady_clock::time_point aFrom, std::chrono::steady_clock::time_point aTo) noexcept;
} // namespace DeathCause
