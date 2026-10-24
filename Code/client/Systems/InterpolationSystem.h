#pragma once

struct World;
struct Actor;

/**
 * @brief Manages interpolation of movement and animations.
 */
struct InterpolationSystem
{
    // aNow: the local network clock in ms (TransportService::GetLastPollTime), which advances the actor's playback clock.
    // aDeadAtOwner: the owner's game has said the actor is dead (RemoteComponent::DeadAtOwner).
    // aLastActionTick: the owner's tick of the last action played on the actor (RemoteAnimationComponent::LastRanAction), 0 for none.
    static void Update(Actor* apActor, InterpolationComponent& aInterpolationComponent, double aNow, bool aDeadAtOwner, uint64_t aLastActionTick) noexcept;
    // aArrivalTime: when the update carrying the point arrived (TransportService::GetMessageReceiveTime).
    static void AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint, double aArrivalTime) noexcept;
    // The owner tick this actor is shown at, 0 until its first point arrived.
    static uint64_t GetPlaybackTick(const InterpolationComponent& acInterpolationComponent) noexcept;
    static InterpolationComponent& Setup(World& aWorld, entt::entity aEntity) noexcept;
    static void Clean(World& aWorld, entt::entity aEntity) noexcept;
    // The copy on this form id is gone, or this game runs the actor now: a quiet stop kept for it no longer applies.
    // Left behind, it would start or stop the next copy given that id.
    static void Forget(uint32_t aFormId) noexcept;
};
