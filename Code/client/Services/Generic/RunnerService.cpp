#include <TiltedOnlinePCH.h>

#include <Services/RunnerService.h>

#include <Events/UpdateEvent.h>

RunnerService::RunnerService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
}

void RunnerService::Queue(std::function<void()> aFunctor) noexcept
{
    m_runner.Add(std::move(aFunctor));
}

void RunnerService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    // One at a time, not Drain, which keeps the queue's lock while its tasks run.
    // A task that reaches the game can queue an event from a hook, and under that lock the game would freeze.
    std::function<void()> task;
    while (m_runner.Pop(task))
        task();
}
