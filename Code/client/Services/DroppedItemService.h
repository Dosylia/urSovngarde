#pragma once

#include <Events/EventDispatcher.h>

#include <Structs/Inventory.h>

struct World;
struct TransportService;
struct ConnectedEvent;
struct DisconnectedEvent;
struct CellChangeEvent;
struct ItemDroppedEvent;
struct ItemPickedUpEvent;
struct NotifyDroppedItem;
struct NotifyDroppedItemRemoved;
struct NotifyDroppedItemMove;
struct NotifyWorldObjectMove;
struct UpdateEvent;
struct TESObjectREFR;

/**
 * @brief Keeps items players drop lying in every player's world, including items dropped in earlier sessions.
 *
 * Reports local drops and pick-ups to the server, and adopts the server's items if already here or places them.
 * Passes on the movement of dropped items and of placed world objects (a bottle, a cart).
 * Also announces older drops, read from the game's ExtraDroppedItemList on the player.
 */
class DroppedItemService
{
public:
    DroppedItemService(World&, entt::dispatcher&, TransportService&);

private:
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnCellChange(const CellChangeEvent&) noexcept;
    void OnItemDropped(const ItemDroppedEvent&) noexcept;
    void OnItemPickedUp(const ItemPickedUpEvent&) noexcept;
    void OnNotifyDroppedItem(const NotifyDroppedItem&) noexcept;
    void OnNotifyDroppedItemRemoved(const NotifyDroppedItemRemoved&) noexcept;
    void OnNotifyDroppedItemMove(const NotifyDroppedItemMove&) noexcept;
    void OnNotifyWorldObjectMove(const NotifyWorldObjectMove&) noexcept;
    void UpdateWorldObjects(std::chrono::steady_clock::time_point aNow) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;

    //! Send a reference that lies on the floor to the server. False when its place cannot be named to the server.
    bool SendAdd(TESObjectREFR* apRef, const Inventory::Entry& acItem, bool aAnnouncement) noexcept;
    void AnnounceOldDrops() noexcept;

    void MapServerIdToRef(uint32_t aId, uint32_t aRefFormId) noexcept;

    World& m_world;
    TransportService& m_transport;

    //! Server id to the reference that stands for it here, and back.
    std::unordered_map<uint32_t, uint32_t> m_refById;
    std::unordered_map<uint32_t, uint32_t> m_idByRef;

    //! This player's own drops and announcements, waiting for the id the server gives them.
    struct Pending
    {
        uint32_t RefFormId{};
        GameId BaseId{};
        bool Announcement{};
    };
    std::vector<Pending> m_pending;

    //! How each remembered item has been moving here, by server id.
    //! Only one side sends an item's position at a time, so an item moved by the other side is held (keyframed) here.
    struct Motion
    {
        glm::vec3 LastPosition{};
        glm::vec3 LastRotation{};
        //! A world object's 3D rotation where LastPosition was taken.
        //! Its Euler angles can wrap around or lag the 3D, so a restart is judged on this.
        glm::mat3 LastTurn{1.f};
        std::chrono::steady_clock::time_point LastLocalMove{};
        std::chrono::steady_clock::time_point LastRemote{};
        std::chrono::steady_clock::time_point QuietUntil{};
        //! The item's base form when it was mapped.
        //! The game reuses a deleted item's FF id for another object, which must not be moved: it could be an actor.
        uint32_t BaseFormId{};
        bool Moving{};
        bool Held{};
        //! Measurement: the other side's moves applied while held, and how many of them rebuilt the object's 3D.
        uint32_t HeldMoves{};
        uint32_t HeldRebuilds{};
        //! The other side's last update said it was put down.
        //! Still held for a moment, since a still hand sends the same.
        bool RemoteAtRest{};
        //! Its 3D was there on the last pass. A cell loading settles every item in it, which is not a move.
        bool Loaded{};
        //! A 3D load in a fresh drop's first two seconds is part of the throw, not a cell reload.
        std::chrono::steady_clock::time_point FreshDropUntil{};
    };
    std::unordered_map<uint32_t, Motion> m_motion;

    //! Objects placed in the world near the player, by reference id, watched for moves here or held for another player.
    //! The list is rebuilt once a second from the player's cell.
    std::unordered_map<uint32_t, Motion> m_objectMotion;
    std::vector<uint32_t> m_objectCandidates;
    std::chrono::steady_clock::time_point m_candidatesAt{};

    //! Old drops already announced this session, so a cell change does not send them again.
    std::unordered_set<uint32_t> m_announced;
    //! What the player dropped while not connected (reference and item), announced once connected.
    //! The game's own dropped-item list is not found on the player, so the client keeps this one.
    std::vector<std::pair<uint32_t, Inventory::Entry>> m_droppedOffline;
    //! Whether this connection has logged what it found on the player.
    bool m_reportedOldDrops{false};

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_droppedConnection;
    entt::scoped_connection m_pickedUpConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_removedConnection;
    entt::scoped_connection m_moveConnection;
    entt::scoped_connection m_objectMoveConnection;
    entt::scoped_connection m_updateConnection;
};
