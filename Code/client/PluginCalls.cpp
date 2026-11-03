#include <TiltedOnlinePCH.h>

#include <PluginCalls.h>

#include <World.h>
#include <Games/ActorExtension.h>
#include <Games/Skyrim/VRBanner.h>
#include <Services/CharacterService.h>
#include <Services/ObjectService.h>

#include <Messages/RequestPartyPlugin.h>
#include <Messages/RequestPartyValue.h>
#include <Services/TransportService.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

// The calls another plugin can make into this mod.
// It finds them with GetProcAddress on the main module, and does without them when this mod is absent.
// These names and signatures are a promise to those plugins.
// The calls that change the game wait for the main thread, so any thread may call them.

namespace
{
std::mutex s_callsLock;
TiltedPhoques::Vector<std::function<void()>> s_calls;

struct PartyPluginAnswer
{
    bool Known{};
    std::string PlayerWithout{};
    std::chrono::steady_clock::time_point AskedAt{};
};
std::mutex s_answersLock;
std::map<std::string, PartyPluginAnswer> s_partyPluginAnswers;

std::mutex s_valuesLock;
std::map<std::string, float> s_partyValues;

bool IsConnected() noexcept
{
    return entt::locator<World>::has_value() && World::Get().GetTransport().IsConnected();
}

bool IsPartyLeader() noexcept
{
    return IsConnected() && World::Get().GetPartyService().IsLeader();
}

void AddCall(std::function<void()> aCall) noexcept
{
    std::scoped_lock lock(s_callsLock);
    s_calls.push_back(std::move(aCall));
}
} // namespace

// No lock is held while they run, so a plugin the game calls back during one of these can make another.
void RunPluginCalls() noexcept
{
    TiltedPhoques::Vector<std::function<void()>> calls;
    {
        std::scoped_lock lock(s_callsLock);
        calls.swap(s_calls);
    }
    for (const auto& call : calls)
        call();
}

void StorePartyPluginAnswer(const String& acPluginFile, const String& acPlayerWithout) noexcept
{
    std::scoped_lock lock(s_answersLock);
    // One asked before a change of party is not stored: the next call asks again.
    if (const auto it = s_partyPluginAnswers.find(acPluginFile.c_str()); it != s_partyPluginAnswers.end())
        it->second = PartyPluginAnswer{true, acPlayerWithout.c_str()};
}

void ForgetPartyPluginAnswers() noexcept
{
    std::scoped_lock lock(s_answersLock);
    s_partyPluginAnswers.clear();
}

void StorePartyValue(const String& acKey, const float aValue) noexcept
{
    std::scoped_lock lock(s_valuesLock);
    if (s_partyValues.try_emplace(acKey.c_str(), aValue).second)
        spdlog::info("PartyValue: '{}' from the leader, {}", acKey.c_str(), aValue);
    else
        s_partyValues[acKey.c_str()] = aValue;
}

void ForgetPartyValues() noexcept
{
    std::scoped_lock lock(s_valuesLock);
    s_partyValues.clear();
}

extern "C"
{
// A game that is not connected plays alone, and does its own switching and reviving.
__declspec(dllexport) bool urSovngarde_IsConnected() noexcept
{
    return IsConnected();
}

__declspec(dllexport) bool urSovngarde_IsPartyLeader() noexcept
{
    return IsPartyLeader();
}

// True when this game runs that actor, false when it only shows another game's copy.
// An actor no game has announced is this game's.
__declspec(dllexport) bool urSovngarde_IsOwnedHere(uint32_t aFormId) noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    const ActorExtension* pExtension = pActor ? pActor->GetExtension() : nullptr;
    return pExtension && !pExtension->IsRemote();
}

// Whether every player of this game's party has a plugin, by its file name.
// 0: everyone has it, or this game plays alone.
// 1: someone does not, and apName holds that player's name.
// -1: not known yet. The answer comes from the server, so call again on a later frame.
__declspec(dllexport) int32_t urSovngarde_FindPartyMemberWithoutPlugin(const char* acpPluginFile, char* apName, uint32_t aNameSize) noexcept
{
    constexpr auto cAskAgainAfter = std::chrono::seconds(5);

    if (!IsConnected() || !acpPluginFile)
        return 0;

    {
        const auto cNow = std::chrono::steady_clock::now();
        std::scoped_lock lock(s_answersLock);
        auto& answer = s_partyPluginAnswers[acpPluginFile];
        if (answer.Known)
        {
            if (answer.PlayerWithout.empty())
                return 0;
            if (apName && aNameSize)
                strncpy_s(apName, aNameSize, answer.PlayerWithout.c_str(), _TRUNCATE);
            return 1;
        }
        // A disconnect can lose the question, so one left unanswered is asked again.
        if (cNow - answer.AskedAt < cAskAgainAfter)
            return -1;
        answer.AskedAt = cNow;
    }

    AddCall(
        [cPluginFile = String(acpPluginFile)]
        {
            RequestPartyPlugin request{};
            request.PluginFile = cPluginFile;
            World::Get().GetTransport().Send(request);
        });
    return -1;
}

// Shows the card of the death screen in this game only, on its next frame, connected or not.
// acpSubtitle may be null or empty for none, aRgb is 0xRRGGBB, and aHoldSeconds counts the time between the fades.
// A new card or banner replaces the one showing.
// True when it was taken; a card needs a title.
__declspec(dllexport) bool urSovngarde_ShowCard(const char* acpTitle, const char* acpSubtitle, uint32_t aRgb, float aHoldSeconds) noexcept
{
    constexpr float cLongestHold = 60.f;

    if (!acpTitle || !*acpTitle)
        return false;

    AddCall([cTitle = std::string(acpTitle), cSubtitle = std::string(acpSubtitle ? acpSubtitle : ""), aRgb, cHold = std::clamp(aHoldSeconds, 0.f, cLongestHold)]
            { VRBanner::ShowDeath(cTitle, cSubtitle, aRgb, cHold); });
    return true;
}

// Shows a bar near the top of the view in this game only, from its next frame until urSovngarde_HideMeter, connected or not.
// One not shown again for 10 s fades out, so a plugin keeps calling while it should stay.
// aFill runs from 0 to 1: the attackers' colour fills that much of the bar from the left, and the defenders' colour the rest.
// acpLabel may be null or empty for none, and the colours are 0xRRGGBB.
// Called again, it changes in place; a card shows beside it, never in its place.
// True when it was taken; a fill must be a number.
__declspec(dllexport) bool urSovngarde_ShowMeter(const char* acpLabel, float aFill, uint32_t aAttackRgb, uint32_t aDefendRgb) noexcept
{
    if (std::isnan(aFill))
        return false;

    AddCall([cLabel = std::string(acpLabel ? acpLabel : ""), cFill = std::clamp(aFill, 0.f, 1.f), aAttackRgb, aDefendRgb]
            { VRBanner::ShowMeter(cLabel, cFill, aAttackRgb, aDefendRgb); });
    return true;
}

__declspec(dllexport) void urSovngarde_HideMeter() noexcept
{
    AddCall([] { VRBanner::HideMeter(); });
}

// Sets a number every game of the party reads with urSovngarde_GetPartyValue, for players who join later too; the newest of a key wins.
// Only the party's leader can set one; a game that plays alone cannot, and keeps its number itself.
// A key holds at most 63 bytes and a party at most 32 keys.
// True when it was taken; a value must be finite.
__declspec(dllexport) bool urSovngarde_SetPartyValue(const char* acpKey, float aValue) noexcept
{
    if (!acpKey || !*acpKey || std::strlen(acpKey) > RequestPartyValue::kLongestKey || !std::isfinite(aValue))
        return false;

    // Sent while the lock is held, so two threads setting one key send in the order they stored.
    // The leader is checked under it too: a game leaving its party forgets the values after it stops leading.
    std::scoped_lock lock(s_valuesLock);
    if (!IsPartyLeader())
        return false;
    const auto cIt = s_partyValues.find(acpKey);
    if (cIt == s_partyValues.end() && s_partyValues.size() >= RequestPartyValue::kMostKeys)
        return false;
    if (cIt != s_partyValues.end() && cIt->second == aValue)
        return true;
    s_partyValues[acpKey] = aValue;
    AddCall(
        [cKey = String(acpKey), aValue]
        {
            RequestPartyValue request{};
            request.Key = cKey;
            request.Value = aValue;
            World::Get().GetTransport().Send(request);
        });
    return true;
}

// The number the party's leader set under that key, in any game of the party, the leader's own included.
// False when none has been set, which is always the case in a game that plays alone.
__declspec(dllexport) bool urSovngarde_GetPartyValue(const char* acpKey, float* apValue) noexcept
{
    if (!acpKey || !apValue)
        return false;

    std::scoped_lock lock(s_valuesLock);
    const auto cIt = s_partyValues.find(acpKey);
    if (cIt == s_partyValues.end())
        return false;
    *apValue = cIt->second;
    return true;
}

// Switches a placed actor off or on in every game of the party, for players who join later too.
// True when the switch was taken, which only the party's leader can ask for.
__declspec(dllexport) bool urSovngarde_SwitchActor(uint32_t aFormId, bool aEnabled) noexcept
{
    if (!IsPartyLeader())
        return false;

    AddCall([aFormId, aEnabled] { World::Get().ctx().at<ObjectService>().SwitchActor(aFormId, aEnabled); });
    return true;
}

// Revives a dead actor in every game of the party, whichever game owns it.
// One switched off is revived when it is switched on.
// True when the revival was taken, which only the party's leader can ask for.
__declspec(dllexport) bool urSovngarde_ReviveActor(uint32_t aFormId) noexcept
{
    if (!IsPartyLeader())
        return false;

    AddCall([aFormId] { World::Get().GetCharacterService().ReviveActor(aFormId); });
    return true;
}
}
