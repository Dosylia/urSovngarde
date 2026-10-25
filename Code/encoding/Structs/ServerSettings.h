#pragma once

using TiltedPhoques::Buffer;

struct ServerSettings
{
    bool operator==(const ServerSettings& acRhs) const noexcept;
    bool operator!=(const ServerSettings& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t Difficulty{};
    bool GreetingsEnabled{};
    bool PvpEnabled{};
    bool SyncPlayerHomes{};
    bool DeathSystemEnabled{};
    bool SyncPlayerCalendar{};
    bool AutoPartyJoin{};
    // A sleep can be granted (bEnableSleep on a private server); clients only report combat then.
    bool SleepEnabled{};
    // VR: more menus run unpaused online (skills, map; UI.cpp kOptionalUnpausedMenus), with the fixes that needs.
    bool UnpausedMenus{};
};
