#include <Services/ReviveService.h>

#include <World.h>
#include <Components.h>
#include <Utils.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <Messages/ReviveRequest.h>
#include <Messages/NotifyDownedState.h>

#include <Services/PartyService.h>
#include <Services/TransportService.h>

#include <Actor.h>
#include <PlayerCharacter.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/VRBodySync.h>
#include <Games/Skyrim/VRHaptics.h>
#endif

namespace
{
// How long a hand has to stay on the downed player.
constexpr auto cHoldTime = std::chrono::seconds(4);
// The middle of a body kneeling in bleedout, above where it stands, and how close a hand must come to it.
constexpr float cBodyCentreHeight = 50.f;
constexpr float cTouchUnits = 50.f;
// Flat Skyrim has no hands to hold out: standing this close counts.
constexpr float cStandByUnits = 100.f;
// A hold already started ends only this much further out, so a hand trembling at the edge does not restart the hold over and over.
constexpr float cLetGoMargin = 20.f;

// Why the server refused, as the reviver reads it.
const char* RefusalText(const ReviveRefusal aRefusal) noexcept
{
    switch (aRefusal)
    {
    case ReviveRefusal::kNone: break;
    case ReviveRefusal::kSelf: return "you cannot revive yourself";
    case ReviveRefusal::kTargetNotDown: return "they are not down";
    case ReviveRefusal::kTargetOutOfTime: return "it is too late";
    case ReviveRefusal::kReviverDown: return "you are down yourself";
    case ReviveRefusal::kNotSameParty: return "they are not in your party";
    case ReviveRefusal::kNotSamePlace: return "they are somewhere else";
    case ReviveRefusal::kTooFar: return "you are too far from them";
    }
    return "the server said no";
}
} // namespace

ReviveService::ReviveService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&ReviveService::OnUpdate>(this))
    , m_disconnectedConnection(aDispatcher.sink<DisconnectedEvent>().connect<&ReviveService::OnDisconnected>(this))
    , m_partyLeftConnection(aDispatcher.sink<PartyLeftEvent>().connect<&ReviveService::OnPartyLeft>(this))
    , m_downedStateConnection(aDispatcher.sink<NotifyDownedState>().connect<&ReviveService::OnNotifyDownedState>(this))
{
}

void ReviveService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (!m_revivedChecks.empty())
        LogRevivedCopies(cNow);

    if (m_downed.empty())
        return;

    // Ten times a second: the hold is seconds long, and each check is also one haptic tick.
    if (cNow < m_nextCheck)
        return;
    m_nextCheck = cNow + std::chrono::milliseconds(100);

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || pPlayer->IsDead() || pPlayer->actorState.IsBleedingOut())
    {
        StopHolding();
        return;
    }

    uint32_t touched = 0;
    int hand = -1;
    float touchedDistance = 0.f;
    for (const uint32_t cPlayerId : m_downed)
    {
        float distance = -1.f;
        const int cHand = TouchingHand(FindCopy(cPlayerId), cPlayerId == m_holdTarget, distance);
        if (cPlayerId == m_holdTarget)
            m_holdDistance = distance;
        if (cHand >= 0 && !touched)
        {
            touched = cPlayerId;
            hand = cHand;
            touchedDistance = distance;
        }
    }

    if (touched != m_holdTarget)
    {
        StopHolding();
        if (touched)
        {
            m_holdTarget = touched;
            m_holdSince = cNow;
            spdlog::info("Revive: holding a hand on player {} '{}', {:.0f} units from its body", touched, NameOf(touched).c_str(), touchedDistance);
            Utils::ShowHudMessage(String("urSovngarde: reviving ") + NameOf(touched) + ", keep your hand on them");
            // The downed player learns someone is on it.
            ReviveRequest held{};
            held.TargetPlayerId = touched;
            held.HoldStage = ReviveRequest::kHoldStart;
            m_transport.Send(held);
        }
        return;
    }

    if (!m_holdTarget || m_asked)
        return;

    const float cProgress = std::chrono::duration<float>(cNow - m_holdSince) / std::chrono::duration<float>(cHoldTime);
#ifdef SKYRIMVR
    // A tick on the touching hand, stronger as the revive comes closer.
    VRHaptics::Pulse(hand == 1, static_cast<uint16_t>(500.f + std::min(cProgress, 1.f) * 3499.f));
#endif
    if (cProgress < 1.f)
        return;

    ReviveRequest request{};
    request.TargetPlayerId = m_holdTarget;
    m_transport.Send(request);
    m_asked = true;
    spdlog::info("Revive: held a hand on player {} '{}' long enough; asking to revive them", m_holdTarget, NameOf(m_holdTarget).c_str());
}

void ReviveService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_downed.clear();
    m_revivedChecks.clear();
    StopHolding();
}

// Out of the party, its downed members are nobody this player can revive; on joining one, the server tells who is down in it.
void ReviveService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    m_downed.clear();
    StopHolding();
}

void ReviveService::OnNotifyDownedState(const NotifyDownedState& acMessage) noexcept
{
    if (acMessage.PlayerId == m_transport.GetLocalPlayerId())
        return;

    const String who = NameOf(acMessage.PlayerId);

    // This player's revive was refused: still down, so the hold starts over, and a hand still on them asks again.
    if (acMessage.Change == NotifyDownedState::kReviveRefused)
    {
        if (acMessage.ReviverId != m_transport.GetLocalPlayerId())
            return;
        spdlog::info("Revive: the server refused the revive of player {} '{}': {}", acMessage.PlayerId, who.c_str(), RefusalText(acMessage.Refusal));
        Utils::ShowHudMessage(String("urSovngarde: cannot revive ") + who + ", " + RefusalText(acMessage.Refusal));
        if (m_holdTarget == acMessage.PlayerId)
        {
            m_holdSince = std::chrono::steady_clock::now();
            m_asked = false;
        }
        return;
    }
    const auto cKnown = std::find(m_downed.begin(), m_downed.end(), acMessage.PlayerId);
    if (cKnown != m_downed.end())
        m_downed.erase(cKnown);
    if (m_holdTarget == acMessage.PlayerId)
        StopHolding();

    switch (acMessage.Change)
    {
    case NotifyDownedState::kDown:
        m_downed.push_back(acMessage.PlayerId);
        spdlog::info("Revive: player {} '{}' is down; {} s to revive them", acMessage.PlayerId, who.c_str(), acMessage.SecondsLeft);
#ifdef SKYRIMVR
        Utils::ShowHudMessage(String("urSovngarde: ") + who + " is down; hold a hand on them to revive them");
#else
        Utils::ShowHudMessage(String("urSovngarde: ") + who + " is down; stand by them to revive them");
#endif
        break;
    case NotifyDownedState::kRevived:
        spdlog::info("Revive: player {} '{}' was revived by player {}", acMessage.PlayerId, who.c_str(), acMessage.ReviverId);
        if (acMessage.ReviverId == m_transport.GetLocalPlayerId())
            Utils::ShowHudMessage(String("urSovngarde: you revived ") + who);
        else
            Utils::ShowHudMessage(String("urSovngarde: ") + NameOf(acMessage.ReviverId) + " revived " + who);
        m_revivedChecks.push_back({acMessage.PlayerId, std::chrono::steady_clock::now() + std::chrono::seconds(5)});
        break;
    case NotifyDownedState::kRespawned: spdlog::info("Revive: player {} '{}' respawned", acMessage.PlayerId, who.c_str()); break;
    case NotifyDownedState::kDied:
        spdlog::info("Revive: player {} '{}' was not revived in time", acMessage.PlayerId, who.c_str());
        Utils::ShowHudMessage(String("urSovngarde: ") + who + " was not revived in time");
        break;
    case NotifyDownedState::kStoodUp: spdlog::info("Revive: player {} '{}' is up again without a revive", acMessage.PlayerId, who.c_str()); break;
    case NotifyDownedState::kLeft:
        spdlog::info("Revive: player {} '{}' left the party or the server while down; nobody here can revive them", acMessage.PlayerId, who.c_str());
        Utils::ShowHudMessage(String("urSovngarde: ") + who + " left while down");
        break;
    case NotifyDownedState::kPartyFallen:
    case NotifyDownedState::kReviveRefused:
    case NotifyDownedState::kReviveHeld:
    case NotifyDownedState::kReviveLetGo: break;
    }
}

Actor* ReviveService::FindCopy(const uint32_t aPlayerId) const noexcept
{
    auto view = m_world.view<FormIdComponent, PlayerComponent>();
    for (const auto cEntity : view)
        if (view.get<PlayerComponent>(cEntity).Id == aPlayerId)
            return Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id));
    return nullptr;
}

int ReviveService::TouchingHand(Actor* apCopy, const bool aHeld, float& aDistance) const noexcept
{
    aDistance = -1.f;
    if (!apCopy || !apCopy->GetNiNode())
        return -1;
    const float cMargin = aHeld ? cLetGoMargin : 0.f;

    const NiPoint3 cAt = apCopy->position;
#ifdef SKYRIMVR
    const glm::vec3 cBodyCentre(cAt.x, cAt.y, cAt.z + cBodyCentreHeight);
    int nearest = -1;
    for (uint8_t side = 0; side < 2; ++side)
    {
        glm::vec3 hand{};
        if (!VRBodySync::LocalHandPosition(side, hand))
            continue;
        const float cDistance = glm::distance(hand, cBodyCentre);
        if (aDistance < 0.f || cDistance < aDistance)
        {
            aDistance = cDistance;
            nearest = side;
        }
    }
    return aDistance >= 0.f && aDistance <= cTouchUnits + cMargin ? nearest : -1;
#else
    const NiPoint3 cPlayerAt = PlayerCharacter::Get()->position;
    aDistance = glm::distance(glm::vec3(cAt.x, cAt.y, cAt.z), glm::vec3(cPlayerAt.x, cPlayerAt.y, cPlayerAt.z));
    return aDistance <= cStandByUnits + cMargin ? 1 : -1;
#endif
}

String ReviveService::NameOf(const uint32_t aPlayerId) const noexcept
{
    const auto& players = m_world.GetPartyService().GetPlayers();
    const auto cName = players.find(aPlayerId);
    return cName != players.end() ? cName->second : String("A party member");
}

// TEMPORARY: whether a revived party member's copy is up again here, for a copy seen still lying down after a revive.
// The head's height above the copy's root says whether it stands (about 105) or lies (under 30).
void ReviveService::LogRevivedCopies(const std::chrono::steady_clock::time_point aNow) noexcept
{
    for (auto it = m_revivedChecks.begin(); it != m_revivedChecks.end();)
    {
        if (aNow < it->second)
        {
            ++it;
            continue;
        }
        const uint32_t cPlayerId = it->first;
        it = m_revivedChecks.erase(it);
        Actor* pCopy = FindCopy(cPlayerId);
        if (!pCopy)
        {
            spdlog::info("Revive: player {} '{}' 5 s after the revive: no copy of them here", cPlayerId, NameOf(cPlayerId).c_str());
            continue;
        }
#ifdef SKYRIMVR
        const std::string cBody = VRBodySync::DescribeBody(pCopy);
#else
        const std::string cBody = "body not described on this build";
#endif
        spdlog::info("Revive: player {} '{}' 5 s after the revive, copy {:X} here: bleedout {}, dead {}; {}", cPlayerId, NameOf(cPlayerId).c_str(), pCopy->formID,
                     pCopy->actorState.IsBleedingOut(), pCopy->IsDead(), cBody);
    }
}

void ReviveService::StopHolding() noexcept
{
    if (m_holdTarget && !m_asked)
    {
        spdlog::info("Revive: hand taken off player {} '{}' before the revive, {:.0f} units from its body", m_holdTarget, NameOf(m_holdTarget).c_str(), m_holdDistance);
        ReviveRequest letGo{};
        letGo.TargetPlayerId = m_holdTarget;
        letGo.HoldStage = ReviveRequest::kHoldStop;
        m_transport.Send(letGo);
    }
    m_holdTarget = 0;
    m_asked = false;
}
