#pragma once

// The plugins whose records make or change NPCs and levelled NPC lists: the plugin that adds each record and every plugin that edits it.
// Two games without the same ones roll different creatures and people in the same places.
namespace NpcPlugins
{
//! Read from the game's loaded records once, then kept; empty when they could not be read.
const Vector<String>& Get() noexcept;
} // namespace NpcPlugins
