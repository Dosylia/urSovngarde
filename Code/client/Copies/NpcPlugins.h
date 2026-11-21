#pragma once

// The plugins whose records make or change NPCs and levelled NPC lists: the plugin that adds each record and every plugin that edits it.
// Two games without the same ones roll different creatures and people in the same places.
namespace NpcPlugins
{
/**
 * List the plugins that add or change NPCs or levelled NPC lists, sorted by name.
 * Read from the game's loaded records on the first call, then kept; nothing is listed when a read faults.
 *
 * @return The plugins' file names; empty when the game's records could not be read.
 */
const Vector<String>& Get() noexcept;
} // namespace NpcPlugins
