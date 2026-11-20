#pragma once

#include <windows.h>

#include <CrashHandler.h>

#include <string>

/**
 * Reads game memory that may already be freed, and answers false or a short text instead of crashing.
 * Used by the watches that look at nodes the game may be destroying on another thread.
 */
namespace GuardedRead
{
/**
 * Read one value at an address, without crashing when the address cannot be read.
 * The crash handler is told the fault is expected, or it would report a crash first.
 *
 * @param apAddress Where the value is.
 * @param aOut Receives the value; left as it was when the read faults.
 * @return True when the value was read.
 */
template <class T> bool Value(const void* apAddress, T& aOut) noexcept
{
    bool read = false;
    CrashGuard::Enter();
    __try
    {
        aOut = *static_cast<const T*>(apAddress);
        read = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    CrashGuard::Leave();
    return read;
}

/**
 * Copy a game text that may be freed or have no end, for a log line.
 * The copy stops at the end of the text, at the first byte that cannot be read, or after 63 characters.
 *
 * @param apText The text, or null.
 * @return The readable start of the text; empty for null.
 */
std::string Text(const char* apText) noexcept;
} // namespace GuardedRead
