#include <TiltedOnlinePCH.h>

#include <Services/Generic/NpcEquipmentCheck.h>

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
struct PendingCheck
{
    uint32_t FormId{};
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory Sent{};
    std::chrono::steady_clock::time_point Due{};
    uint32_t ChecksLeft{};
    std::chrono::steady_clock::time_point SentAt{};
    bool Undone{};
};
TiltedPhoques::Vector<PendingCheck> s_pending;

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

bool IsWorn(const Inventory& acInventory, const GameId& acItemId) noexcept
{
    return std::any_of(acInventory.Entries.begin(), acInventory.Entries.end(), [&acItemId](const Inventory::Entry& acEntry) { return acEntry.BaseId == acItemId && acEntry.IsWorn(); });
}

void Note(const uint32_t aFormId, const uint32_t aServerId, const uint32_t aOwnershipEpoch, const Inventory& acSent, const bool aUndone) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(), [aFormId](const PendingCheck& acCheck) { return acCheck.FormId == aFormId; }), s_pending.end());
    // A bound for a fight with many NPCs changing gear at once: the oldest checks go first.
    if (s_pending.size() >= 256)
        s_pending.erase(s_pending.begin());
    s_pending.push_back({aFormId, aServerId, aOwnershipEpoch, acSent, cNow + std::chrono::seconds(1), 2, cNow, aUndone});
}

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
        it->Due = cNow + std::chrono::seconds(2);
        ++it;
    }
}

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
