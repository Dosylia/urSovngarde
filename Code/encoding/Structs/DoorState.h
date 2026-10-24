#pragma once

#include <cstdint>

//! A door's state as the game numbers it (TESObjectREFR::OpenState on the client), as it travels in messages.
namespace DoorState
{
enum : uint8_t
{
    kNone = 0,
    kOpen = 1,
    kOpening = 2,
    kClosed = 3,
    kClosing = 4,
};

//! The state an activation leaves a door in: a closed or closing door opens, an open or opening one closes.
//! kNone when the state before is not known.
constexpr uint8_t AfterActivation(const uint8_t aBefore) noexcept
{
    switch (aBefore)
    {
    case kClosed:
    case kClosing: return kOpen;
    case kOpen:
    case kOpening: return kClosed;
    default: return kNone;
    }
}
} // namespace DoorState
