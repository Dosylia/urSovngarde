// The checks a code patch makes before it writes over a game instruction (client/PatchCheck.h).

#include <catch2/catch.hpp>

#include <PatchCheck.h>

#include <cstdint>
#include <cstring>

namespace
{
// Three pages side by side: executable, data only, and reserved without memory behind it.
struct Pages
{
    static constexpr size_t cPage = 0x1000;

    Pages()
    {
        pBase = static_cast<uint8_t*>(VirtualAlloc(nullptr, 3 * cPage, MEM_RESERVE, PAGE_NOACCESS));
        if (!pBase)
            return;
        VirtualAlloc(pBase, cPage, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        VirtualAlloc(pBase + cPage, cPage, MEM_COMMIT, PAGE_READWRITE);
    }

    ~Pages()
    {
        if (pBase)
            VirtualFree(pBase, 0, MEM_RELEASE);
    }

    uint8_t* Code() const noexcept { return pBase; }
    uint8_t* Data() const noexcept { return pBase + cPage; }
    uint8_t* Reserved() const noexcept { return pBase + 2 * cPage; }

    uint8_t* pBase = nullptr;
};

// Writes "call apTarget" at apAt.
void PutCall(uint8_t* apAt, const uint8_t* apTarget)
{
    apAt[0] = 0xE8;
    const auto cDisplacement = static_cast<int32_t>(apTarget - (apAt + 5));
    std::memcpy(apAt + 1, &cDisplacement, sizeof(cDisplacement));
}
} // namespace

TEST_CASE("A patch accepts a call only when it leads to code", "[patchcheck]")
{
    Pages pages;
    REQUIRE(pages.Code());
    uint8_t* pAt = pages.Code();

    PutCall(pAt, pages.Code() + 0x100);
    CHECK(PatchCheck::IsCall(pAt));

    // Bytes that only start with E8, as when the offset is wrong: the target is data, or nothing at all.
    PutCall(pAt, pages.Data() + 0x100);
    CHECK_FALSE(PatchCheck::IsCall(pAt));
    PutCall(pAt, pages.Reserved() + 0x100);
    CHECK_FALSE(PatchCheck::IsCall(pAt));

    // A jump where a call was expected.
    PutCall(pAt, pages.Code() + 0x100);
    pAt[0] = 0xE9;
    CHECK_FALSE(PatchCheck::IsCall(pAt));

    CHECK_FALSE(PatchCheck::IsCall(nullptr));
}

TEST_CASE("A patch accepts only the bytes it expects", "[patchcheck]")
{
    // mov rbx, [rsp+0x58], the projectile handle read.
    const uint8_t cRead[]{0x48, 0x8B, 0x5C, 0x24, 0x58};
    CHECK(PatchCheck::Is(cRead, {0x48, 0x8B, 0x5C, 0x24, 0x58}));
    CHECK_FALSE(PatchCheck::Is(cRead, {0x48, 0x8B, 0x5C, 0x24, 0x50}));
    CHECK_FALSE(PatchCheck::Is(nullptr, {0x48}));

    // jne +0x1D, the favorites menu's branch; then the same place after another plugin's jump.
    const uint8_t cBranch[]{0x75, 0x1D};
    CHECK(PatchCheck::IsShortConditionalJump(cBranch));
    const uint8_t cJump[]{0xEB, 0x1D};
    CHECK_FALSE(PatchCheck::IsShortConditionalJump(cJump));
    // A near conditional jump is six bytes, and two NOPs would leave four of them behind.
    const uint8_t cNearBranch[]{0x0F, 0x85, 0x10, 0x00, 0x00, 0x00};
    CHECK_FALSE(PatchCheck::IsShortConditionalJump(cNearBranch));
    CHECK_FALSE(PatchCheck::IsShortConditionalJump(nullptr));
}

TEST_CASE("A patch left off names the bytes it found", "[patchcheck]")
{
    const uint8_t cCall[]{0xE8, 0x8F, 0x1C, 0x00, 0x00};
    CHECK(PatchCheck::Bytes(cCall, 5) == "e8 8f 1c 00 00");
    CHECK(PatchCheck::Bytes(nullptr, 5) == "no address");

    uint8_t function[0x40]{};
    CHECK(PatchCheck::At(function, 0x15) == function + 0x15);
    CHECK(PatchCheck::At(nullptr, 0x15) == nullptr);
}
