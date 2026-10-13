#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//! Admins on the public server (urSovngarde-hub PUBLIC_SERVER.md, "Admins on the public server"; decided 2026-10-12:
//! one key per admin). An admin's connect token is "urs-admin1:" and the admin's key, 64 lowercase hex characters; the
//! server knows only each key's id, the SHA-256 of those 64 characters as text, from URSOVNGARDE_ADMIN_KEYS
//! ("emma:<id>,seen:<id>", read once at start). Nothing here logs a token or a key.
//!
//! The hash is handed in, so this holds no crypto of its own and the tests need none (GameServer passes OpenSSL's).
namespace AdminKeys
{
using Digest = std::array<uint8_t, 32>;
using Hash = Digest (*)(std::string_view acText);

struct Entry
{
    std::string Name;
    Digest Id{};
};

constexpr std::string_view kPrefix = "urs-admin1:";

//! The admins of an URSOVNGARDE_ADMIN_KEYS value. A malformed entry is left out and its position (1 for the first)
//! goes to aSkipped: a name is a to z, 0 to 9, - and _, 1 to 32 of them; an id is 64 lowercase hex characters.
std::vector<Entry> Parse(std::string_view acList, std::vector<size_t>& aSkipped);

//! Whether a token is meant as an admin key (starts with the prefix), well formed or not.
[[nodiscard]] bool IsKeyToken(std::string_view acToken) noexcept;

//! The admin a key token belongs to, or null: the token's 64 hex characters hashed and compared with every id, in time
//! that does not depend on where, or whether, they differ. Null for a malformed token too.
const Entry* Match(std::string_view acToken, const std::vector<Entry>& acEntries, Hash aHash) noexcept;
} // namespace AdminKeys
