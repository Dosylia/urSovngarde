#pragma once

struct World;
struct NotifyDrawWeapon;
struct CharacterSpawnRequest;

// Another player's weapon, drawn or sheathed on its copy as its owner has it.
// The copy draws by replaying its owner's draw, and a paused game refuses replays: a sword drawn while this game was paused never showed.
namespace PlayerCopyWeapon
{
// The owner's weapon state, relayed by the server at each change.
void OnNotify(const NotifyDrawWeapon& acMessage) noexcept;

// A copy sent again (its owner back in range) comes with the server's current state, so an older one kept here is dropped.
void OnSpawn(const CharacterSpawnRequest& acMessage) noexcept;

// Twice a second: a copy that has shown the other state for 1.5 s while this game runs is drawn or sheathed, twice at most for each state received.
void Run(World& aWorld, bool aConnected) noexcept;
} // namespace PlayerCopyWeapon
