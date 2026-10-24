#pragma once

#include <cstdint>

#ifdef SKYRIMVR
bool SleepWaitHooksReady() noexcept;
uint64_t SleepWaitMenuGeneration();
void RunDeferredSleepWaitAccept();
void UpdateSleepWatchControls();
// Called by CalendarService after changing ownership of the native clock.
void ResolveDeferredSleepWait(bool aAllowed);
#endif
