#pragma once

struct World;
struct Actor;

/**
 * @brief Manages interpolation of movement and animations.
 */
struct InterpolationSystem
{
    // aNow: the local network clock in ms (TransportService::GetLastPollTime), which advances the actor's playback clock.
    static void Update(Actor* apActor, InterpolationComponent& aInterpolationComponent, double aNow) noexcept;
    // aArrivalTime: when the update carrying the point arrived (TransportService::GetMessageReceiveTime).
    static void AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint, double aArrivalTime) noexcept;
    // The owner tick this actor is shown at, 0 until its first point arrived.
    static uint64_t GetPlaybackTick(const InterpolationComponent& acInterpolationComponent) noexcept;
    static InterpolationComponent& Setup(World& aWorld, entt::entity aEntity) noexcept;
    static void Clean(World& aWorld, entt::entity aEntity) noexcept;
};
