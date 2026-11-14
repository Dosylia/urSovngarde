#pragma once

#include <Events/PacketEvent.h>
#include <Structs/ActorPresence.h>

struct World;
struct UpdateEvent;
struct RequestActorPresence;
struct OwnerComponent;
struct CellIdComponent;
struct Player;

/**
 * @brief What each game has loaded, from the report each game sends every 2 s, and who may run a character next.
 * A game's own report is the truth; the loaded cells the server works out are a guess, wrong for an actor a game keeps beyond them.
 */
struct PresenceService
{
    PresenceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PresenceService() noexcept = default;

    TP_NOCOPYMOVE(PresenceService);

    //! A game that sent no movement for this long is paused or loading.
    static bool IsSilent(const Player* apPlayer, std::chrono::steady_clock::time_point aNow) noexcept;

    //! How that player's game has the character, by its latest report; nothing when that report is stale or leaves it out.
    std::optional<ActorPresenceKind> LoadedAs(entt::entity aEntity, uint32_t aPlayerId, std::chrono::steady_clock::time_point aNow) const noexcept;

    //! Who may run the character next, best first: a game with the reference, then one showing it on a stand-in, then one whose loaded cells take it in.
    //! A silent game comes after all of those.
    TiltedPhoques::Vector<Player*> NextOwners(entt::entity aEntity, const OwnerComponent& acOwner, const CellIdComponent& acCell, bool aIsDragon,
                                              std::chrono::steady_clock::time_point aNow) const noexcept;

    //! Whom its owner's game last reported the character fighting, a server id; 0 for nobody, or when that report is stale or the target gone.
    uint32_t FightOf(entt::entity aEntity) const noexcept;

    //! That game let the character go, so its last report no longer stands.
    void ForgetReport(entt::entity aEntity, uint32_t aPlayerId) const noexcept;

    //! That character leaves the server, so the character is handed on fighting nobody.
    void ForgetFightWith(entt::entity aEntity, entt::entity aCharacter) const noexcept;

    //! TEMPORARY: logs a removal made while another game still has the character loaded, for the census.
    void LogRemovalWhileSeen(entt::entity aEntity, const char* acpReason) const noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) const noexcept;
    void OnActorPresenceRequest(const PacketEvent<RequestActorPresence>& acMessage) const noexcept;

private:
    //! TEMPORARY: every 30 s, what each game has loaded against what the server gives it.
    void LogPresenceCensus() const noexcept;

    World& m_world;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_presenceConnection;
};
