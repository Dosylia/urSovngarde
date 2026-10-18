#pragma once

#include <cstdint>

struct NiPoint3;

//! The game's own sound descriptors (SNDR forms), played through its audio manager.
namespace GameSound
{
//! A sound the game is playing (BSSoundHandle, CommonLibVR-NG): kept to stop a looping one.
struct Handle
{
    uint32_t SoundId = 0xFFFFFFFF;
    bool AssumeSuccess = false;
    uint8_t Pad05 = 0;
    uint16_t Pad06 = 0;
    uint32_t State = 0;
};

//! Plays the sound descriptor with that form id, at that point, or where the game puts a sound of its own when null.
//! A looping descriptor goes on until Stop. Returns false when the form is not a sound descriptor or the game refused it.
bool Play(uint32_t aDescriptorId, const NiPoint3* apAt, Handle* apOut = nullptr) noexcept;

//! Stops a sound Play started; false when it was not playing.
bool Stop(Handle& aHandle) noexcept;
} // namespace GameSound
