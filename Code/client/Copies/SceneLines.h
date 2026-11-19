#pragma once

#include <chrono>
#include <cstdint>

// Whether this game's own copy of a scene speaks, so a scene line from the speaker's owner plays once.
namespace SceneLines
{
//! A line one of this game's NPC copies said in a scene of its own.
void NoteCopyLine(uint32_t aSceneId) noexcept;
//! Whether any copy said a line of that scene at or after that moment.
bool SceneSpokeSince(uint32_t aSceneId, std::chrono::steady_clock::time_point aSince) noexcept;
} // namespace SceneLines
