#pragma once

#include <Structs/Tints.h>

struct World;
struct TransportService;

// How this player looks to the others: the copy every other game makes of their character.
namespace PlayerLook
{
// The player's own skin and warpaint colours, which a copy of the player is made with.
Tints FaceTints() noexcept;

/**
 * Send this player's look and name to the server, which rebuilds their copy in the other games and renames the player.
 *
 * @param aWorld The world that holds the player's entity.
 * @param aTransport The connection to the server.
 * @return False when the player has no server character yet.
 */
bool SendRenamed(World& aWorld, TransportService& aTransport) noexcept;
} // namespace PlayerLook
