#pragma once

// The game's spin lock: the id of the thread holding it and how many times that thread took it.
// Taken and released the way the game does (CommonLibSSE's BSSpinLock), so this client and the game can share one.
struct BSSpinLock
{
    volatile uint32_t OwningThread;
    volatile long LockCount;

    void Lock() noexcept;
    void Unlock() noexcept;
};

static_assert(sizeof(BSSpinLock) == 0x8);

struct BSScopedSpinLock
{
    explicit BSScopedSpinLock(BSSpinLock& aLock) noexcept
        : m_lock(aLock)
    {
        m_lock.Lock();
    }
    ~BSScopedSpinLock() noexcept { m_lock.Unlock(); }

    BSScopedSpinLock(const BSScopedSpinLock&) = delete;
    BSScopedSpinLock& operator=(const BSScopedSpinLock&) = delete;

private:
    BSSpinLock& m_lock;
};
