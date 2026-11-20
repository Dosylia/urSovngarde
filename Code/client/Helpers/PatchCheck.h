#pragma once

// Checks a game instruction before a patch writes over it.
// A patch written over other bytes corrupts the game's code; a check that fails only leaves one feature off.
// Another plugin may have patched the same place first, so the bytes are not always the game's own.
//
// Header-only, without the game, so that the tests can check it.

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

namespace PatchCheck
{
//! The address aOffset bytes into a function, or null when the function's address is not known.
inline uint8_t* At(void* apFunction, size_t aOffset) noexcept
{
    return apFunction ? static_cast<uint8_t*>(apFunction) + aOffset : nullptr;
}

//! True when the bytes at apAddress are exactly acExpected.
inline bool Is(const uint8_t* apAddress, std::initializer_list<uint8_t> acExpected) noexcept
{
    return apAddress && std::memcmp(apAddress, acExpected.begin(), acExpected.size()) == 0;
}

//! True when apAddress holds a relative call (E8) whose target is executable memory.
//! The target can be outside the game: a plugin that swapped the call first points it at its own code.
inline bool IsCall(const uint8_t* apAddress) noexcept
{
    if (!apAddress || apAddress[0] != 0xE8)
        return false;

    int32_t displacement;
    std::memcpy(&displacement, apAddress + 1, sizeof(displacement));
    const uintptr_t cTarget = reinterpret_cast<uintptr_t>(apAddress) + 5 + static_cast<intptr_t>(displacement);

    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(cTarget), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

//! True when apAddress holds a two-byte conditional jump: 70 to 7F, then its offset.
inline bool IsShortConditionalJump(const uint8_t* apAddress) noexcept
{
    return apAddress && apAddress[0] >= 0x70 && apAddress[0] <= 0x7F;
}

//! The aCount bytes at apAddress for a log line, such as "e8 8f 1c 00 00".
inline std::string Bytes(const uint8_t* apAddress, size_t aCount)
{
    if (!apAddress)
        return "no address";

    constexpr char cDigits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < aCount; ++i)
    {
        if (i)
            text += ' ';
        text += cDigits[apAddress[i] >> 4];
        text += cDigits[apAddress[i] & 0xF];
    }
    return text;
}

/**
 * Check if a function starts with another plugin's hook: a relative jump (E9) or an indirect one (FF 25).
 * MinHook carries such a jump into its trampoline, so a hook of ours can go on top of it.
 *
 * @param apAddress The function's first byte, or null.
 * @return True when it starts with one of those jumps.
 */
inline bool IsHookJump(const uint8_t* apAddress) noexcept
{
    return Is(apAddress, {0xE9}) || Is(apAddress, {0xFF, 0x25});
}

/**
 * Describe another plugin's hook at a function's start, for the line that says ours went on.
 * Read before our hook goes on: afterwards the first bytes are our own jump.
 *
 * @param apAddress The function's first byte, or null.
 * @return " (on top of another plugin's hook: e9 ...)" with its first six bytes, or nothing when no such hook is there.
 */
inline std::string OtherHookNote(const uint8_t* apAddress)
{
    return IsHookJump(apAddress) ? " (on top of another plugin's hook: " + Bytes(apAddress, 6) + ")" : std::string();
}
} // namespace PatchCheck
