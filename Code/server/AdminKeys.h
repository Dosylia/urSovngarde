#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//! Public server admin keys: the connect token is "urs-admin1:" plus a 64-character lowercase hex key.
//! The server holds only each key's SHA-256 id, from URSOVNGARDE_ADMIN_KEYS ("name:<id>,name:<id>").
//! The hash function is passed in, so this file has no crypto and the tests need none. Nothing here logs a key.
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

//! Parses URSOVNGARDE_ADMIN_KEYS; a malformed entry is skipped and its 1-based position added to aSkipped.
//! A name is 1 to 32 of a-z, 0-9, - and _; an id is 64 lowercase hex characters.
std::vector<Entry> Parse(std::string_view acList, std::vector<size_t>& aSkipped);

//! Whether a token is meant as an admin key (starts with the prefix), well formed or not.
[[nodiscard]] bool IsKeyToken(std::string_view acToken) noexcept;

//! The admin a key token belongs to, or null; the hash is compared with every id in constant time.
const Entry* Match(std::string_view acToken, const std::vector<Entry>& acEntries, Hash aHash) noexcept;
} // namespace AdminKeys
