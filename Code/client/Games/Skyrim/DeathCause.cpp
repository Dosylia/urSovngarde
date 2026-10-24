#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/DeathCause.h>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <PlayerCharacter.h>

#include <array>
#include <mutex>

namespace DeathCause
{
namespace
{
// Enough to look back past the blows that land while the player is already down.
constexpr size_t kKept = 16;

std::mutex s_lock;
std::array<Hit, kKept> s_hits{};
size_t s_next = 0;
bool s_heard = false;

void Keep(const Hit& acHit) noexcept
{
    std::scoped_lock lock(s_lock);
    s_hits[s_next] = acHit;
    s_next = (s_next + 1) % kKept;
}

struct HitSink final : BSTEventSink<TESHitEvent>
{
    BSTEventResult OnEvent(const TESHitEvent* apEvent, const EventDispatcher<TESHitEvent>*) override
    {
        const PlayerCharacter* pPlayer = PlayerCharacter::Get();
        if (!apEvent || !pPlayer || apEvent->hit != pPlayer || !apEvent->hitter || apEvent->hitter == pPlayer)
            return BSTEventResult::kOk;

        Hit hit{};
        hit.At = std::chrono::steady_clock::now();
        hit.AttackerFormId = apEvent->hitter->formID;
        hit.SourceFormId = apEvent->source;
        hit.ProjectileFormId = apEvent->projectile;
        hit.Flags = apEvent->flags;
        Keep(hit);

        // Once, so a log shows the game's hits reach here.
        if (!s_heard)
        {
            s_heard = true;
            spdlog::info("DeathCause: the game's hits on the player arrive (attacker {:X}, source {:X}, projectile {:X}, flags {})", hit.AttackerFormId, hit.SourceFormId,
                         hit.ProjectileFormId, hit.Flags);
        }
        return BSTEventResult::kOk;
    }
};

HitSink s_sink;
bool s_started = false;
} // namespace

void Start() noexcept
{
    if (s_started)
        return;
    s_started = true;
    EventDispatcherManager::Get()->hitEvent.RegisterSink(&s_sink);
}

void RecordPlayerHit(const uint32_t aAttackerPlayerId) noexcept
{
    Hit hit{};
    hit.At = std::chrono::steady_clock::now();
    hit.AttackerPlayerId = aAttackerPlayerId;
    Keep(hit);
}

std::optional<Hit> LastBetween(const std::chrono::steady_clock::time_point aFrom, const std::chrono::steady_clock::time_point aTo) noexcept
{
    std::scoped_lock lock(s_lock);
    std::optional<Hit> last;
    for (const Hit& cHit : s_hits)
        if (cHit.At != std::chrono::steady_clock::time_point{} && cHit.At >= aFrom && cHit.At <= aTo && (!last || cHit.At > last->At))
            last = cHit;
    return last;
}
} // namespace DeathCause
