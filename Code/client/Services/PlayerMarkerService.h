#pragma once

#include <Structs/GameId.h>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct NotifyPlayerWhereabouts;
struct TESObjectREFR;
struct TESObjectCELL;
struct TESQuest;
struct Actor;

/**
 * @brief Other players as world map icons ("<name>, near Whiterun") from NotifyPlayerWhereabouts, hidden when in sight.
 * Uses SkyrimTogether.esp's records (Tools/VR/player-markers.py).
 */
struct PlayerMarkerService
{
    PlayerMarkerService(World& aWorld, entt::dispatcher& aDispatcher);
    ~PlayerMarkerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerMarkerService);

    static constexpr size_t kMapMarkers = 32;

protected:
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnWhereabouts(const NotifyPlayerWhereabouts& acMessage) noexcept;

private:
    // One marker and the player it shows, if any.
    struct Slot
    {
        TESObjectREFR* pMarker{nullptr};
        uint32_t PlayerId{0}; // 0: free
        String Text;
        GameId CellId{};       // where the server last placed them, when the marker follows that
        uint32_t BodyCellId{0}; // the cell of their body in our game, when the marker follows that instead
        glm::vec3 Position{};
        bool Parked{true}; // waiting in the holding cell, on no map
    };

    bool FindForms() noexcept;
    void StopQuest() noexcept;
    void FollowServerPosition(Slot& aSlot, size_t aIndex, const GameId& acWorldSpaceId, const GameId& acCellId, const glm::vec3& acPosition) noexcept;
    void FollowBody(Slot& aSlot, Actor* apActor) noexcept;
    void Park(Slot& aSlot, size_t aIndex) noexcept;
    void Free(Slot& aSlot, size_t aIndex) noexcept;
    void SetMapMarkerName(TESObjectREFR* apMarker, const String& acText) noexcept;
    void FreeAll() noexcept;

    World& m_world;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_whereaboutsConnection;

    bool m_formsLooked{false};
    bool m_formsFound{false};
    bool m_connected{false};
    bool m_questStarted{false};
    uint32_t m_modBase{0};
    TESQuest* m_pQuest{nullptr};
    TESObjectCELL* m_pHoldingCell{nullptr};
    std::array<Slot, kMapMarkers> m_mapSlots{};
    std::unordered_map<uint32_t, String> m_names;
};
