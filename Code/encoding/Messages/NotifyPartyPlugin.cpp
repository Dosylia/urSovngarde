#include <Messages/NotifyPartyPlugin.h>

void NotifyPartyPlugin::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, PluginFile);
    Serialization::WriteString(aWriter, PlayerWithout);
}

void NotifyPartyPlugin::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PluginFile = Serialization::ReadString(aReader);
    PlayerWithout = Serialization::ReadString(aReader);
}
