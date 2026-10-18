#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

// Only copy the hours out of the UI callback: its arguments and menu can be destroyed before Main::Update.
class DeferredSleepWait
{
public:
    uint64_t GetGeneration()
    {
        std::scoped_lock lock(m_lock);
        return m_generation;
    }

    bool HasPending()
    {
        std::scoped_lock lock(m_lock);
        return m_hours.has_value();
    }

    void Open(const void* apMenu)
    {
        std::scoped_lock lock(m_lock);
        m_pMenu = apMenu;
        m_hours.reset();
        m_allowed.reset();
        m_started = false;
        ++m_generation;
    }

    void Close(const void* apMenu)
    {
        std::scoped_lock lock(m_lock);
        if (m_pMenu == apMenu)
        {
            m_pMenu = nullptr;
            m_hours.reset();
            m_allowed.reset();
        }
    }

    void Resolve(bool aAllowed)
    {
        std::scoped_lock lock(m_lock);
        m_allowed = aAllowed;
        if (!aAllowed)
            m_hours.reset();
    }

    void Queue(const void* apMenu, double aHours)
    {
        std::scoped_lock lock(m_lock);
        if (apMenu && m_pMenu == apMenu && !m_started && !m_hours && m_allowed != false && std::isfinite(aHours) && aHours >= 1.0 && aHours <= 24.0)
            m_hours = aHours;
    }

    std::optional<double> Take(const void* apMenu)
    {
        std::scoped_lock lock(m_lock);
        if (!apMenu || m_pMenu != apMenu)
        {
            m_hours.reset();
            return std::nullopt;
        }
        // A fast click must wait for the server to grant sleep and release the clock lock.
        if (m_allowed != true)
            return std::nullopt;
        const auto cHours = m_hours;
        m_hours.reset();
        if (cHours)
            m_started = true;
        return cHours;
    }

private:
    std::mutex m_lock;
    const void* m_pMenu = nullptr;
    std::optional<double> m_hours;
    std::optional<bool> m_allowed;
    bool m_started = false;
    uint64_t m_generation = 0;
};
