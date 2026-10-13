#pragma once

#include <Events/PacketEvent.h>

struct World;
struct VoiceDataRequest;
struct VoiceStateRequest;

/**
 * @brief Relays each voice piece, unreliable, to nearby players with voice on and to the speaker's party.
 */
struct VoiceService
{
    VoiceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~VoiceService() noexcept = default;

    TP_NOCOPYMOVE(VoiceService);

protected:
    void OnVoiceState(const PacketEvent<VoiceStateRequest>& acMessage) const noexcept;
    void OnVoiceData(const PacketEvent<VoiceDataRequest>& acMessage) noexcept;

private:
    bool CanHear(const Player& acListener, const Player& acSpeaker) const noexcept;

    struct Budget
    {
        std::chrono::steady_clock::time_point Since{};
        uint32_t Packets{0};
    };

    World& m_world;
    // Pieces per speaker this second; past the limit the rest of the second is dropped.
    // Steam sends about 17 a second while talking.
    Map<uint32_t, Budget> m_budgets;

    entt::scoped_connection m_voiceStateConnection;
    entt::scoped_connection m_voiceDataConnection;
};
