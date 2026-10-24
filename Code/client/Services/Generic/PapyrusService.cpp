#include <TiltedOnlinePCH.h>

#include <Events/PapyrusFunctionRegisterEvent.h>

#include <Services/PapyrusService.h>

#include <World.h>

#include <algorithm>
#include <cctype>

namespace
{
// The key a native is recorded and looked up by, in lower case.
// The game's string pool ignores case and keeps the first spelling it was given, so a name can be registered as "objectReference".
String FunctionKey(const String& acNamespace, const String& acFunction) noexcept
{
    String key = acNamespace + "::" + acFunction;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char aChar) { return static_cast<char>(std::tolower(aChar)); });
    return key;
}
} // namespace

PapyrusService::PapyrusService(entt::dispatcher& aDispatcher) noexcept
{
    m_papyrusFunctionRegisterConnection = aDispatcher.sink<PapyrusFunctionRegisterEvent>().connect<&PapyrusService::HandlePapyrusFunctionEvent>(this);
}

const void* PapyrusService::Get(const String& acNamespace, const String& acFunction) const noexcept
{
    std::shared_lock lock(m_lock);
    const auto itor = m_functions.find(FunctionKey(acNamespace, acFunction));
    if (itor != std::end(m_functions))
        return itor->second;

    return nullptr;
}

const void* PapyrusLookup::Resolve() const noexcept
{
    if (const void* pFunction = m_pFunction.load(std::memory_order_acquire))
        return pFunction;

    const auto& papyrus = World::Get().ctx().at<PapyrusService>();
    const void* pFunction = papyrus.Get(m_pScope, m_pName);
    if (!pFunction)
    {
        if (!m_reported.test_and_set())
            spdlog::error("Papyrus: {}.{} not found among the {} functions recorded; not called until it is", m_pScope, m_pName, papyrus.FunctionCount());
        return nullptr;
    }

    m_pFunction.store(pFunction, std::memory_order_release);

    // Once, so every log says how many natives were recorded by the time they were first used.
    static std::atomic_flag s_counted = ATOMIC_FLAG_INIT;
    if (!s_counted.test_and_set())
        spdlog::info("Papyrus: {} native functions recorded at the first call ({}.{})", papyrus.FunctionCount(), m_pScope, m_pName);
    return pFunction;
}

size_t PapyrusService::FunctionCount() const noexcept
{
    std::shared_lock lock(m_lock);
    return m_functions.size();
}

void PapyrusService::HandlePapyrusFunctionEvent(const PapyrusFunctionRegisterEvent& acEvent) noexcept
{
    std::unique_lock lock(m_lock);
    m_functions[FunctionKey(acEvent.Namespace, acEvent.Name)] = acEvent.Function;
}
