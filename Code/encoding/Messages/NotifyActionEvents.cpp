#include <Messages/NotifyActionEvents.h>

#include <DecodeGuard.h>

#include <TiltedCore/Serialization.hpp>

void NotifyActionEvents::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Actions.size());

    for (const auto& [serverId, actions] : Actions)
    {
        Serialization::WriteVarInt(aWriter, serverId);
        Serialization::WriteVarInt(aWriter, actions.size());

        for (const auto& action : actions)
            action.GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void NotifyActionEvents::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    // Counts read from the network are bounded, and an actor named twice is a damaged message: the factory drops it.
    constexpr uint64_t cMaxActors = 1024;
    constexpr uint64_t cMaxActions = 4096;
    const auto cCount = Serialization::ReadVarInt(aReader);
    if (!DecodeGuard::Within(cCount, cMaxActors))
        return;
    uint64_t total = 0;
    for (uint64_t i = 0; i < cCount; ++i)
    {
        const uint32_t cServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        const auto cActions = Serialization::ReadVarInt(aReader);
        total += cActions;
        if (Actions.find(cServerId) != Actions.end() || !DecodeGuard::Within(total, cMaxActions))
        {
            DecodeGuard::Fail();
            Actions.clear();
            return;
        }

        auto& actions = Actions[cServerId];
        actions.resize(cActions);
        for (auto& action : actions)
            action.ApplyDifferential(aReader);
    }
}
