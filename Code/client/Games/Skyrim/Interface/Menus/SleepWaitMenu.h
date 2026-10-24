#pragma once

#include <cstdint>

#ifdef SKYRIMVR
// True while the server lets a sleep pass time: the menu is then shown without pausing the game.
bool SleepWaitCanRunUnpaused() noexcept;
uint64_t SleepWaitMenuGeneration();
void RunDeferredSleepWaitAccept();
void UpdateSleepWatchControls();
// Called by CalendarService after changing ownership of the native clock.
void ResolveDeferredSleepWait(bool aAllowed);
#endif
