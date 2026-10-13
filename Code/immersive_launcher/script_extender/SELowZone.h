// Copyright (C) 2021 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.
#pragma once

#ifdef SKYRIMVR
namespace script_extender
{
// Blocks the address space the Script Extender's dll must not be relocated into, leaving gaps for its trampoline allocator.
// Call immediately before loading the dll: held longer than the load, it pushes the heap into the range that allocator searches.
void ReserveLowZone();

// Hands that address space back. Safe to call more than once.
void ReleaseLowZone();
} // namespace script_extender
#endif
