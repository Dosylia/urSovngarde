#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct ProjectileLaunchedEvent;
struct NotifyProjectileLaunch;
struct HitEvent;
struct Actor;

/**
 * @brief Responsible for projectiles, combat agro, etc.
 */
struct CombatService
{
    CombatService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher);
    ~CombatService() noexcept = default;

    TP_NOCOPYMOVE(CombatService);

    //! A living NPC this game runs, which is not bleeding out: only the owner runs an NPC's AI, and players are never made to fight.
    static bool CanFight(Actor* apActor) noexcept;
    //! Sets an NPC that can fight on a target alive and drawn here; one already fighting is turned to it, never stopped first.
    bool SetOnTarget(Actor* apActor, Actor* apTarget) const noexcept;
    //! Whom an NPC this game runs fights, by server id; 0 for nobody, or a target without one.
    uint32_t FightOf(Actor* apActor) const noexcept;
    //! An NPC hit by another player fights that player too.
    void FightBack(Actor* apActor, uint32_t aAttackerPlayerId, float aDeltaHealth) const noexcept;
    //! A character gained while its last owner's game had it fighting fights the same one here.
    void CarryOnFight(Actor* apActor, uint32_t aServerId, uint32_t aTargetServerId) const noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) const noexcept;
    void OnLocalComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    void OnProjectileLaunchedEvent(const ProjectileLaunchedEvent& acEvent) const noexcept;
    void OnNotifyProjectileLaunch(const NotifyProjectileLaunch& acMessage) const noexcept;
    void OnHitEvent(const HitEvent& acEvent) const noexcept;

    void RunTargetUpdates(const float acDelta) const noexcept;

private:
    World& m_world;
    TransportService& m_transport;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_localComponentRemoved;
    entt::scoped_connection m_projectileLaunchedConnection;
    entt::scoped_connection m_projectileLaunchConnection;
    entt::scoped_connection m_hitConnection;
};
