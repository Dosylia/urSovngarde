#include <catch2/catch.hpp>

#include <AdminKeys.h>

#include <string>

namespace
{
// A stand-in for SHA-256: the key's first 32 hex characters read as bytes. Enough to tell keys apart here.
AdminKeys::Digest FakeHash(const std::string_view acText)
{
    AdminKeys::Digest digest{};
    for (size_t i = 0; i < digest.size() && i < acText.size(); ++i)
        digest[i] = static_cast<uint8_t>(acText[i]);
    return digest;
}

// The id FakeHash gives a key, as URSOVNGARDE_ADMIN_KEYS writes it.
std::string IdOf(const std::string& acKey)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (const uint8_t b : FakeHash(acKey))
    {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

const std::string kPlayer1Key(64, 'a');
const std::string kPlayer2Key = std::string(32, 'b') + std::string(32, 'c');
} // namespace

TEST_CASE("Admin keys", "[server][admin]")
{
    std::vector<size_t> skipped;
    const auto entries = AdminKeys::Parse("player1:" + IdOf(kPlayer1Key) + ", player2:" + IdOf(kPlayer2Key), skipped);
    REQUIRE(entries.size() == 2);
    REQUIRE(skipped.empty());

    SECTION("a listed key is that admin")
    {
        const AdminKeys::Entry* pFound = AdminKeys::Match(std::string(AdminKeys::kPrefix) + kPlayer2Key, entries, &FakeHash);
        REQUIRE(pFound);
        REQUIRE(pFound->Name == "player2");
    }

    SECTION("another well-formed key is nobody")
    {
        REQUIRE(AdminKeys::IsKeyToken(std::string(AdminKeys::kPrefix) + std::string(64, 'd')));
        REQUIRE_FALSE(AdminKeys::Match(std::string(AdminKeys::kPrefix) + std::string(64, 'd'), entries, &FakeHash));
    }

    SECTION("a malformed key token is nobody")
    {
        REQUIRE_FALSE(AdminKeys::Match(std::string(AdminKeys::kPrefix) + "abc", entries, &FakeHash));
        REQUIRE_FALSE(AdminKeys::Match(std::string(AdminKeys::kPrefix) + std::string(64, 'A'), entries, &FakeHash));
    }

    SECTION("a token without the prefix is not a key")
    {
        REQUIRE_FALSE(AdminKeys::IsKeyToken(kPlayer1Key));
        REQUIRE_FALSE(AdminKeys::IsKeyToken("friends-password"));
        REQUIRE_FALSE(AdminKeys::Match(kPlayer1Key, entries, &FakeHash));
    }
}

TEST_CASE("Admin key list", "[server][admin]")
{
    SECTION("malformed entries are skipped by position, the others kept")
    {
        std::vector<size_t> skipped;
        const auto entries = AdminKeys::Parse("Player1:" + IdOf(kPlayer1Key) + ",player2:" + IdOf(kPlayer2Key) + ",bad:123,:" + IdOf(kPlayer1Key), skipped);
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].Name == "player2");
        REQUIRE(skipped == std::vector<size_t>{1, 3, 4});
    }

    SECTION("empty or missing means no admins")
    {
        std::vector<size_t> skipped;
        REQUIRE(AdminKeys::Parse("", skipped).empty());
        REQUIRE(AdminKeys::Parse("  ", skipped).empty());
        REQUIRE(skipped.empty());
        REQUIRE_FALSE(AdminKeys::Match(std::string(AdminKeys::kPrefix) + kPlayer1Key, {}, &FakeHash));
    }
}
