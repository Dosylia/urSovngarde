#pragma once

#include <mutex>
#include <optional>

// The UI worker publishes a confirmation; the main thread consumes it for the same open menu.
class DeferredSleepWait
{
public:
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
    }

    void Close(const void* apMenu)
    {
        std::scoped_lock lock(m_lock);
        if (m_pMenu == apMenu)
        {
            m_pMenu = nullptr;
            m_hours.reset();
        }
    }

    void Queue(const void* apMenu, double aHours)
    {
        std::scoped_lock lock(m_lock);
        if (apMenu && m_pMenu == apMenu && !m_hours)
            m_hours = aHours;
    }

    std::optional<double> Take(const void* apMenu)
    {
        std::scoped_lock lock(m_lock);
        const auto cHours = apMenu && m_pMenu == apMenu ? m_hours : std::nullopt;
        m_hours.reset();
        return cHours;
    }

private:
    std::mutex m_lock;
    const void* m_pMenu = nullptr;
    std::optional<double> m_hours;
};
