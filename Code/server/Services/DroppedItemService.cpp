#include <Services/DroppedItemService.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>

#include <Messages/RequestDroppedItemAdd.h>
#include <Messages/RequestDroppedItemRemove.h>
#include <Messages/NotifyDroppedItem.h>
#include <Messages/NotifyDroppedItemRemoved.h>
#include <Messages/RequestDroppedItemMove.h>
#include <Messages/NotifyDroppedItemMove.h>
#include <Messages/RequestWorldObjectMove.h>
#include <Messages/NotifyWorldObjectMove.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/ShiftGridCellRequest.h>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <fstream>

namespace
{
// Next to the server, where it is started from. Deleting it forgets every dropped item and nothing else.
constexpr const char* kFile = "dropped_items.bin";
constexpr uint32_t kMagic = 0x504F5244; // "DROP"
// Version 2 added each item's rotation. A version 1 file is still read, with rotation zero.
constexpr uint64_t kVersion = 2;

//! How close an announced item must lie to a known one of the same base and place to count as the same item.
//! Generous, because the item may have settled or been reloaded since.
constexpr float kSameItemRadius = 200.f;

CellIdComponent CellOf(const DroppedItemService::Item& acItem) noexcept
{
    return {acItem.CellId, acItem.WorldSpaceId, GridCellCoords::CalculateGridCellCoords(acItem.Position)};
}
} // namespace

DroppedItemService::DroppedItemService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_addConnection(aDispatcher.sink<PacketEvent<RequestDroppedItemAdd>>().connect<&DroppedItemService::OnAdd>(this))
    , m_removeConnection(aDispatcher.sink<PacketEvent<RequestDroppedItemRemove>>().connect<&DroppedItemService::OnRemove>(this))
    , m_moveConnection(aDispatcher.sink<PacketEvent<RequestDroppedItemMove>>().connect<&DroppedItemService::OnMove>(this))
    , m_objectMoveConnection(aDispatcher.sink<PacketEvent<RequestWorldObjectMove>>().connect<&DroppedItemService::OnObjectMove>(this))
    , m_interiorConnection(aDispatcher.sink<PacketEvent<EnterInteriorCellRequest>>().connect<&DroppedItemService::OnEnterInteriorCell>(this))
    , m_exteriorConnection(aDispatcher.sink<PacketEvent<EnterExteriorCellRequest>>().connect<&DroppedItemService::OnEnterExteriorCell>(this))
    , m_gridConnection(aDispatcher.sink<PacketEvent<ShiftGridCellRequest>>().connect<&DroppedItemService::OnShiftGridCell>(this))
{
    Load();
}

const DroppedItemService::Item* DroppedItemService::FindSameItem(const Inventory::Entry& acEntry, const GameId& acCellId, const GameId& acWorldSpaceId,
                                                             const Vector3_NetQuantize& acPosition) const noexcept
{
    for (const auto& [id, item] : m_items)
    {
        if (item.Entry.BaseId != acEntry.BaseId)
            continue;

        // Outdoors an item can roll into another cell, so the worldspace must match; indoors the cell must.
        const bool samePlace = acWorldSpaceId ? item.WorldSpaceId == acWorldSpaceId : item.CellId == acCellId;
        if (samePlace && glm::distance(glm::vec3(item.Position), glm::vec3(acPosition)) <= kSameItemRadius)
            return &item;
    }
    return nullptr;
}

void DroppedItemService::OnAdd(const PacketEvent<RequestDroppedItemAdd>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;

    if (message.Item.BaseId == GameId{} || message.Item.Count == 0)
        return;

    // An announced item may already be known: answering with its id lets the client adopt it instead of doubling it.
    if (message.Announcement)
    {
        if (const Item* pKnown = FindSameItem(message.Item, message.CellId, message.WorldSpaceId, message.Position))
        {
            SendItem(acMessage.pPlayer, *pKnown);
            return;
        }
    }

    Item item{};
    item.Id = m_nextId++;
    item.Entry = message.Item;
    // A drop arrives as a negative inventory change; the item on the floor is a count.
    item.Entry.Count = std::abs(item.Entry.Count);
    item.CellId = message.CellId;
    item.WorldSpaceId = message.WorldSpaceId;
    item.Position = message.Position;
    item.Rotation = message.Rotation;

    const auto [it, inserted] = m_items.emplace(item.Id, item);
    Save();

    spdlog::info("Dropped item {} registered: {:X}:{:X} x{} by player {:X}{}, cell {:X}, worldspace {:X}, at ({:.0f}, {:.0f}, {:.0f})", item.Id,
                 item.Entry.BaseId.ModId, item.Entry.BaseId.BaseId, item.Entry.Count, acMessage.pPlayer->GetId(),
                 message.Announcement ? " (announced from an earlier drop)" : "", item.CellId.BaseId, item.WorldSpaceId.BaseId, item.Position.x,
                 item.Position.y, item.Position.z);

    // Sent to the dropper as well, so its client learns the item's id.
    const CellIdComponent cell = CellOf(it->second);
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == acMessage.pPlayer || pPlayer->GetCellComponent().IsInRange(cell, false))
            SendItem(pPlayer, it->second);
    }
}

void DroppedItemService::OnRemove(const PacketEvent<RequestDroppedItemRemove>& acMessage) noexcept
{
    const auto it = m_items.find(acMessage.Packet.Id);
    if (it == m_items.end())
        return; // somebody else picked it up first, and the removal is already on its way

    spdlog::info("Dropped item {} picked up by player {:X}", it->first, acMessage.pPlayer->GetId());
    m_items.erase(it);
    Save();

    // Sent to everyone, not only those in range: a player out of range may still have it in their save.
    NotifyDroppedItemRemoved notify{};
    notify.Id = acMessage.Packet.Id;
    for (Player* pPlayer : m_world.GetPlayerManager())
        pPlayer->Send(notify);
}

void DroppedItemService::OnMove(const PacketEvent<RequestDroppedItemMove>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    const auto it = m_items.find(message.Id);
    if (it == m_items.end())
        return; // picked up meanwhile

    it->second.Position = message.Position;
    it->second.Rotation = message.Rotation;

    // Written to disk only once it stops, not on every update of a held item.
    if (message.AtRest)
        Save();

    NotifyDroppedItemMove notify{};
    notify.Id = message.Id;
    notify.Position = message.Position;
    notify.Rotation = message.Rotation;
    notify.AtRest = message.AtRest;

    const CellIdComponent cell = CellOf(it->second);
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer != acMessage.pPlayer && pPlayer->GetCellComponent().IsInRange(cell, false))
            pPlayer->Send(notify);
    }
}

// A placed world object moved by a player's hand: relayed to everyone in range, and remembered once it stops.
void DroppedItemService::OnObjectMove(const PacketEvent<RequestWorldObjectMove>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    const uint64_t cKey = (static_cast<uint64_t>(message.ObjectId.ModId) << 32) | message.ObjectId.BaseId;

    if (message.AtRest || message.Taken)
        m_objects[cKey] = ObjectPlace{message.ObjectId, message.CellId, message.WorldSpaceId, message.Position, message.Rotation, message.Taken};

    NotifyWorldObjectMove notify{};
    notify.ObjectId = message.ObjectId;
    notify.CellId = message.CellId;
    notify.WorldSpaceId = message.WorldSpaceId;
    notify.Position = message.Position;
    notify.Rotation = message.Rotation;
    notify.AtRest = message.AtRest;
    notify.Taken = message.Taken;

    const CellIdComponent cell{message.CellId, message.WorldSpaceId, GridCellCoords::CalculateGridCellCoords(message.Position)};
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer != acMessage.pPlayer && pPlayer->GetCellComponent().IsInRange(cell, false))
            pPlayer->Send(notify);
    }
}

// Reads the packet, not the player's cell component, which another service updates from the same packet in no set order.
void DroppedItemService::OnEnterInteriorCell(const PacketEvent<EnterInteriorCellRequest>& acMessage) const noexcept
{
    SendInRange(acMessage.pPlayer, CellIdComponent{acMessage.Packet.CellId});
}

void DroppedItemService::OnEnterExteriorCell(const PacketEvent<EnterExteriorCellRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    SendInRange(acMessage.pPlayer, CellIdComponent{message.CellId, message.WorldSpaceId, message.CurrentCoords});
}

void DroppedItemService::OnShiftGridCell(const PacketEvent<ShiftGridCellRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    SendInRange(acMessage.pPlayer, CellIdComponent{message.PlayerCell, message.WorldSpaceId, message.CenterCoords});
}

void DroppedItemService::SendInRange(Player* apPlayer, const CellIdComponent& acViewer) const noexcept
{
    // A client ignores ids it already has, so resending on every cell change is cheap.
    for (const auto& [id, item] : m_items)
    {
        if (acViewer.IsInRange(CellOf(item), false))
            SendItem(apPlayer, item);
    }

    for (const auto& [key, place] : m_objects)
    {
        const CellIdComponent cell{place.CellId, place.WorldSpaceId, GridCellCoords::CalculateGridCellCoords(place.Position)};
        if (!acViewer.IsInRange(cell, false))
            continue;
        NotifyWorldObjectMove notify{};
        notify.ObjectId = place.ObjectId;
        notify.CellId = place.CellId;
        notify.WorldSpaceId = place.WorldSpaceId;
        notify.Position = place.Position;
        notify.Rotation = place.Rotation;
        notify.AtRest = true;
        notify.Taken = place.Taken;
        apPlayer->Send(notify);
    }
}

void DroppedItemService::SendItem(Player* apPlayer, const Item& acItem) const noexcept
{
    NotifyDroppedItem notify{};
    notify.Id = acItem.Id;
    notify.Item = acItem.Entry;
    notify.CellId = acItem.CellId;
    notify.WorldSpaceId = acItem.WorldSpaceId;
    notify.Position = acItem.Position;
    notify.Rotation = acItem.Rotation;
    apPlayer->Send(notify);
}

void DroppedItemService::Save() const noexcept
{
    TiltedPhoques::Buffer buffer(64 + m_items.size() * 256);
    TiltedPhoques::Buffer::Writer writer(&buffer);

    writer.WriteBits(kMagic, 32);
    TiltedPhoques::Serialization::WriteVarInt(writer, kVersion);
    TiltedPhoques::Serialization::WriteVarInt(writer, m_nextId);
    TiltedPhoques::Serialization::WriteVarInt(writer, m_items.size());
    for (const auto& [id, item] : m_items)
    {
        TiltedPhoques::Serialization::WriteVarInt(writer, item.Id);
        item.Entry.Serialize(writer);
        item.CellId.Serialize(writer);
        item.WorldSpaceId.Serialize(writer);
        item.Position.Serialize(writer);
        TiltedPhoques::Serialization::WriteFloat(writer, item.Rotation.x);
        TiltedPhoques::Serialization::WriteFloat(writer, item.Rotation.y);
        TiltedPhoques::Serialization::WriteFloat(writer, item.Rotation.z);
    }

    // Written aside and swapped in, so a crash mid-write leaves the previous list rather than half of one.
    const std::string temporary = std::string(kFile) + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            spdlog::error("Could not write {}: dropped items will not survive a server restart", temporary);
            return;
        }
        file.write(reinterpret_cast<const char*>(buffer.GetData()), static_cast<std::streamsize>(writer.Size()));
    }
    std::error_code error;
    std::filesystem::rename(temporary, kFile, error);
    if (error)
        spdlog::error("Could not replace {}: {}", kFile, error.message());
}

void DroppedItemService::Load() noexcept
{
    std::ifstream file(kFile, std::ios::binary);
    if (!file)
        return; // nothing dropped yet

    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8)
        return;

    TiltedPhoques::ViewBuffer buffer(reinterpret_cast<uint8_t*>(const_cast<char*>(bytes.data())), bytes.size());
    TiltedPhoques::Buffer::Reader reader(&buffer);

    uint64_t magic = 0;
    reader.ReadBits(magic, 32);
    const uint64_t version = TiltedPhoques::Serialization::ReadVarInt(reader);
    if (magic != kMagic || version < 1 || version > kVersion)
    {
        // A file with an unknown layout is not read: guessing could place nonsense in every world.
        spdlog::warn("{} is not a list of dropped items this server can read (version {}, up to {} known); starting without it", kFile, version, kVersion);
        return;
    }

    m_nextId = static_cast<uint32_t>(TiltedPhoques::Serialization::ReadVarInt(reader));
    const uint64_t count = TiltedPhoques::Serialization::ReadVarInt(reader);
    for (uint64_t i = 0; i < count && !reader.Eof(); ++i)
    {
        Item item{};
        item.Id = static_cast<uint32_t>(TiltedPhoques::Serialization::ReadVarInt(reader));
        item.Entry.Deserialize(reader);
        item.CellId.Deserialize(reader);
        item.WorldSpaceId.Deserialize(reader);
        item.Position.Deserialize(reader);
        if (version >= 2)
        {
            item.Rotation.x = TiltedPhoques::Serialization::ReadFloat(reader);
            item.Rotation.y = TiltedPhoques::Serialization::ReadFloat(reader);
            item.Rotation.z = TiltedPhoques::Serialization::ReadFloat(reader);
        }
        m_items.emplace(item.Id, item);
        m_nextId = std::max(m_nextId, item.Id + 1);
    }

    spdlog::info("Loaded {} dropped item(s) from {}", m_items.size(), kFile);
}
