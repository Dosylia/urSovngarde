#pragma once

#include <cstdint>

#ifdef SKYRIMVR
// True while the server lets a sleep pass time: the menu is then shown without pausing the game.
bool SleepWaitCanRunUnpaused() noexcept;
// True while gameplay controls are held: a party member sleeps, or this player's unpaused menu is open.
// Asked by UI.cpp's PlayerControls::CanProcess hook, the only one there.
bool SleepWaitBlocksControls() noexcept;
uint64_t SleepWaitMenuGeneration();
void RunDeferredSleepWaitAccept();
void UpdateSleepWatchControls();
// Called by CalendarService after changing ownership of the native clock.
void ResolveDeferredSleepWait(bool aAllowed);
#endif
