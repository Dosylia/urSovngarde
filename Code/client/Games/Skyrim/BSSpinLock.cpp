#include <BSSpinLock.h>

namespace
{
// While another thread holds it: give up the time slice, and after this many tries sleep a millisecond each time.
constexpr uint32_t kFastSpinThreshold = 10000;
} // namespace

void BSSpinLock::Lock() noexcept
{
    const uint32_t cThread = GetCurrentThreadId();
    _mm_lfence();
    if (OwningThread == cThread)
    {
        _InterlockedIncrement(&LockCount);
        return;
    }

    if (_InterlockedCompareExchange(&LockCount, 1, 0))
    {
        uint32_t spins = 0;
        while (_InterlockedCompareExchange(&LockCount, 1, 0))
            Sleep(++spins < kFastSpinThreshold ? 0 : 1);
        _mm_lfence();
    }

    OwningThread = cThread;
    _mm_sfence();
}

void BSSpinLock::Unlock() noexcept
{
    const uint32_t cThread = GetCurrentThreadId();
    _mm_lfence();
    if (OwningThread != cThread)
        return;

    if (LockCount == 1)
    {
        OwningThread = 0;
        _mm_mfence();
        _InterlockedCompareExchange(&LockCount, 0, 1);
    }
    else
        _InterlockedDecrement(&LockCount);
}
