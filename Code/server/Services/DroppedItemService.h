#pragma once

#include <Events/PacketEvent.h>

#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

struct World;
struct Player;
struct CellIdComponent;
struct RequestDroppedItemAdd;
struct RequestDroppedItemRemove;
struct RequestDroppedItemMove;
struct RequestWorldObjectMove;
struct EnterInteriorCellRequest;
struct EnterExteriorCellRequest;
struct ShiftGridCellRequest;

/**
 * @brief Remembers items players drop, and gives them to everyone who comes near.
 *
 * Each item is sent to everyone in range when dropped and to anyone entering its cell, until somebody picks it up.
 * The list is saved to disk, so items outlive a server restart.
 */
class DroppedItemService
{
public:
    DroppedItemService(World& aWorld, entt::dispatcher& aDispatcher);

    struct Item
    {
        uint32_t Id{};
        Inventory::Entry Entry{};
        GameId CellId{};
        GameId WorldSpaceId{};
        Vector3_NetQuantize Position{};
        glm::vec3 Rotation{};
    };

    //! The saved list, for tests and diagnostics.
    [[nodiscard]] const std::map<uint32_t, Item>& Items() const noexcept { return m_items; }

private:
    void OnAdd(const PacketEvent<RequestDroppedItemAdd>& acMessage) noexcept;
    void OnRemove(const PacketEvent<RequestDroppedItemRemove>& acMessage) noexcept;
    void OnMove(const PacketEvent<RequestDroppedItemMove>& acMessage) noexcept;
    void OnObjectMove(const PacketEvent<RequestWorldObjectMove>& acMessage) noexcept;
    void OnEnterInteriorCell(const PacketEvent<EnterInteriorCellRequest>& acMessage) const noexcept;
    void OnEnterExteriorCell(const PacketEvent<EnterExteriorCellRequest>& acMessage) const noexcept;
    void OnShiftGridCell(const PacketEvent<ShiftGridCellRequest>& acMessage) const noexcept;

    //! Everything within range of where a player now stands, to that player.
    void SendInRange(Player* apPlayer, const CellIdComponent& acViewer) const noexcept;
    void SendItem(Player* apPlayer, const Item& acItem) const noexcept;

    const Item* FindSameItem(const Inventory::Entry& acEntry, const GameId& acCellId, const GameId& acWorldSpaceId,
                         const Vector3_NetQuantize& acPosition) const noexcept;

    void Load() noexcept;
    void Save() const noexcept;

    //! Where a placed world object was last left, or that a player took it, for anyone who comes near later.
    //! Kept in memory only.
    struct ObjectPlace
    {
        GameId ObjectId{};
        GameId CellId{};
        GameId WorldSpaceId{};
        Vector3_NetQuantize Position{};
        glm::vec3 Rotation{};
        bool Taken{};
    };

    World& m_world;
    std::map<uint32_t, Item> m_items;
    std::map<uint64_t, ObjectPlace> m_objects;
    uint32_t m_nextId{1};

    entt::scoped_connection m_addConnection;
    entt::scoped_connection m_removeConnection;
    entt::scoped_connection m_moveConnection;
    entt::scoped_connection m_objectMoveConnection;
    entt::scoped_connection m_interiorConnection;
    entt::scoped_connection m_exteriorConnection;
    entt::scoped_connection m_gridConnection;
};
