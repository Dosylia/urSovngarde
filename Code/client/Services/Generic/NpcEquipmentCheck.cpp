#include <TiltedOnlinePCH.h>

#include <Services/Generic/NpcEquipmentCheck.h>

#include <Services/NpcEquipmentQueue.h>
#include <World.h>
#include <Components.h>
#include <Services/TransportService.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Actor.h>
#include <EquipManager.h>
#include <Forms/TESObjectARMO.h>
#include <Forms/TESBoundObject.h>
#include <Games/ActorExtension.h>
#include <Games/Overrides.h>

namespace
{
// The first check after a change, and the second after the first.
constexpr auto kFirstCheckAfter = std::chrono::seconds(1);
constexpr auto kSecondCheckAfter = std::chrono::seconds(2);
constexpr uint32_t kChecksPerChange = 2;

// The checks waiting, oldest first.
TiltedPhoques::Vector<NpcEquipmentQueue::PendingCheck> s_pending;

/**
 * Find the shield an equipment list has worn.
 *
 * @param acInventory The equipment list.
 * @return The shield, or null when none is worn or it is not loaded in this game.
 */
TESObjectARMO* WornShield(const Inventory& acInventory) noexcept
{
    auto& modSystem = World::Get().GetModSystem();
    for (const auto& entry : acInventory.Entries)
    {
        if (!entry.IsWorn())
            continue;
        auto* pArmor = Cast<TESObjectARMO>(TESForm::GetById(modSystem.GetGameId(entry.BaseId)));
        if (pArmor && pArmor->IsShield())
            return pArmor;
    }
    return nullptr;
}
} // namespace

namespace NpcEquipmentCheck
{
/**
 * Check whether two equipment lists hold the same items, worn in the same hands, with the same spells.
 * Charges and other extra data are not compared.
 *
 * @param acLhs One equipment list.
 * @param acRhs The other equipment list.
 * @return True when both list the same items in the same order, worn the same way, with the same magic equipment.
 */
bool HasSameWornItems(const Inventory& acLhs, const Inventory& acRhs) noexcept
{
    if (acLhs.Entries.size() != acRhs.Entries.size() || !(acLhs.CurrentMagicEquipment == acRhs.CurrentMagicEquipment))
        return false;

    for (size_t i = 0; i < acLhs.Entries.size(); ++i)
    {
        const auto& lhs = acLhs.Entries[i];
        const auto& rhs = acRhs.Entries[i];
        if (lhs.BaseId != rhs.BaseId || lhs.ExtraWorn != rhs.ExtraWorn || lhs.ExtraWornLeft != rhs.ExtraWornLeft)
            return false;
    }

    return true;
}

/**
 * Check whether an equipment list has an item worn, in either hand or as armor.
 *
 * @param acInventory The equipment list.
 * @param acItemId The item, as the server names it.
 * @return True when the list has that item worn.
 */
bool IsWorn(const Inventory& acInventory, const GameId& acItemId) noexcept
{
    return std::any_of(acInventory.Entries.begin(), acInventory.Entries.end(), [&acItemId](const Inventory::Entry& acEntry) { return acEntry.BaseId == acItemId && acEntry.IsWorn(); });
}

/**
 * Queue two checks of what a local NPC wears after one of its equipment changes is sent: 1 s and 3 s later.
 * A difference from what was sent goes out as an equipment snapshot.
 * A check already waiting for that NPC is merged into the new one, so a change the game undid at once is not lost (NpcEquipmentQueue::Queue).
 *
 * @param aFormId The NPC's form id in this game.
 * @param aServerId The NPC's id on the server.
 * @param aOwnershipEpoch The ownership epoch the change was sent with.
 * @param acSent The equipment sent with the change.
 * @param aUndone True when the change was already undone when it was sent, so the first check sends what the NPC wears whatever it finds.
 */
void Note(const uint32_t aFormId, const uint32_t aServerId, const uint32_t aOwnershipEpoch, const Inventory& acSent, const bool aUndone) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    NpcEquipmentQueue::Queue(s_pending, {aFormId, aServerId, aOwnershipEpoch, acSent, cNow + kFirstCheckAfter, kChecksPerChange, cNow, aUndone});
}

/**
 * Run the checks that are due, and send an equipment snapshot for each NPC that wears other items than were sent or had a change undone.
 * A check is dropped when its NPC is no longer this game's at the same epoch, and all are dropped while not connected.
 * Called every frame.
 *
 * @param aWorld The world, for the NPCs this game owns.
 * @param aTransport The connection the snapshots are sent through.
 */
void Run(World& aWorld, TransportService& aTransport) noexcept
{
    if (s_pending.empty())
        return;
    if (!aTransport.IsConnected())
    {
        s_pending.clear();
        return;
    }

    const auto cNow = std::chrono::steady_clock::now();
    auto view = aWorld.view<FormIdComponent, LocalComponent>();
    for (auto it = s_pending.begin(); it != s_pending.end();)
    {
        if (cNow < it->Due)
        {
            ++it;
            continue;
        }

        // Still this game's at the same epoch: otherwise its new owner sends what it wears.
        const bool cStillOurs = std::any_of(view.begin(), view.end(), [&view, &it](const entt::entity aEntity)
        {
            const auto& local = view.get<LocalComponent>(aEntity);
            return view.get<FormIdComponent>(aEntity).Id == it->FormId && local.Id == it->ServerId && local.OwnershipEpoch == it->OwnershipEpoch;
        });
        Actor* pActor = cStillOurs ? Cast<Actor>(TESForm::GetById(it->FormId)) : nullptr;
        if (!pActor || !pActor->GetNiNode() || pActor->GetExtension()->IsRemote())
        {
            it = s_pending.erase(it);
            continue;
        }

        Inventory worn = pActor->GetEquipment();
        if (it->Undone || !HasSameWornItems(worn, it->Sent))
        {
            const TESObjectARMO* pSentShield = WornShield(it->Sent);
            const TESObjectARMO* pWornShield = WornShield(worn);
            spdlog::info("Equipment sync: NPC {:X} {} {} ms after its last change was sent ({} worn entries then, {} now; shield {:X} then, {:X} now); "
                         "its equipment sent again",
                         it->FormId, it->Undone ? "had that change undone at once" : "wears other items", std::chrono::duration_cast<std::chrono::milliseconds>(cNow - it->SentAt).count(),
                         it->Sent.Entries.size(), worn.Entries.size(), pSentShield ? pSentShield->formID : 0, pWornShield ? pWornShield->formID : 0);
            it->Undone = false;

            // No item: the others apply it as a snapshot (InventoryService::OnNotifyEquipmentChanges).
            RequestEquipmentChanges request;
            request.ServerId = it->ServerId;
            request.OwnershipEpoch = it->OwnershipEpoch;
            request.CurrentInventory = worn;
            aTransport.Send(request);
            it->Sent = std::move(worn);
        }

        if (--it->ChecksLeft == 0)
        {
            it = s_pending.erase(it);
            continue;
        }
        it->Due = cNow + kSecondCheckAfter;
        ++it;
    }
}

/**
 * Make an NPC's copy wear the shield its owner wears, or none, from an equipment snapshot.
 * A shield the copy lacks is added to its inventory first; when that fails, the copy keeps its own.
 *
 * @param apActor The copy.
 * @param acOwnerEquipment The equipment its owner's game sent.
 */
void ApplyShield(Actor* apActor, const Inventory& acOwnerEquipment) noexcept
{
    TESObjectARMO* pWanted = WornShield(acOwnerEquipment);
    TESObjectARMO* pWorn = WornShield(apActor->GetWornArmor());
    if (pWanted == pWorn)
        return;

    // The copy may not have it yet (an inventory change that did not reach this game); equipping an item it lacks does nothing.
    if (pWanted && apActor->GetItemCountInInventory(pWanted) <= 0)
    {
        if (auto* pObject = Cast<TESBoundObject>(pWanted))
        {
            ScopedInventoryOverride _;
            apActor->AddObjectToContainer(pObject, nullptr, 1, nullptr);
        }
    }
    if (pWanted && apActor->GetItemCountInInventory(pWanted) <= 0)
    {
        spdlog::info("Equipment sync: remote actor {:X} shield {:X} could not be given to the copy; its own left as it is", apActor->formID, pWanted->formID);
        return;
    }

    auto* pEquipManager = EquipManager::Get();
    if (pWorn)
        pEquipManager->UnEquip(apActor, pWorn, nullptr, 1, nullptr, false, true, false, false, nullptr);
    if (pWanted)
        pEquipManager->Equip(apActor, pWanted, nullptr, 1, nullptr, false, true, false, false);
    spdlog::info("Equipment sync: remote actor {:X} shield from its owner's equipment: {:X} on, {:X} off", apActor->formID, pWanted ? pWanted->formID : 0,
                 pWorn ? pWorn->formID : 0);
}
} // namespace NpcEquipmentCheck
