#pragma once

#include <TiltedCore/Buffer.hpp>

#include <array>
#include <cstdint>

//! The civil war as the party leader's game has it, which the battle quests and their scripts change there only.
struct WarState
{
    //! Forms in Skyrim.esm: the holds' locations, the keyword whose data on them says who owns them, and the journal's war quest.
    static constexpr std::array<uint32_t, 9> kHolds{0x16770, 0x16769, 0x1676E, 0x16772, 0x1676F, 0x1676D, 0x1676B, 0x1676A, 0x1676C};
    static constexpr uint32_t kOwnerKeyword = 0x2A456;
    static constexpr uint32_t kObjectiveQuest = 0x29940;
    static constexpr std::array<uint16_t, 22> kObjectives{10, 11, 101, 102, 201, 202, 301, 302, 401, 402, 501, 502, 601, 602, 701, 702, 801, 802, 901, 902, 1001, 1002};

    //! Neither a faction nor a combination of flags, for what a game has not been told yet.
    static constexpr uint8_t kNothingApplied = 0xFF;

    enum ObjectiveFlags : uint8_t
    {
        kDisplayed = 1 << 0,
        kCompleted = 1 << 1,
        kFailed = 1 << 2
    };

    bool operator==(const WarState& acRhs) const noexcept { return Owners == acRhs.Owners && Objectives == acRhs.Objectives; }

    void Write(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
    {
        for (const uint8_t cOwner : Owners)
            aWriter.WriteBits(cOwner, 8);
        for (const uint8_t cObjective : Objectives)
            aWriter.WriteBits(cObjective, 8);
    }

    void Read(TiltedPhoques::Buffer::Reader& aReader) noexcept
    {
        const auto readByte = [&aReader]()
        {
            uint64_t value = 0;
            aReader.ReadBits(value, 8);
            return static_cast<uint8_t>(value);
        };
        for (uint8_t& owner : Owners)
            owner = readByte();
        for (uint8_t& objective : Objectives)
            objective = readByte();
    }

    //! In the order of kHolds: the faction that owns each.
    std::array<uint8_t, kHolds.size()> Owners{};
    //! In the order of kObjectives: ObjectiveFlags of each.
    std::array<uint8_t, kObjectives.size()> Objectives{};
};
