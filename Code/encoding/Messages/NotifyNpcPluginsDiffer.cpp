#include <Messages/NotifyNpcPluginsDiffer.h>
#include <Structs/PluginList.h>

void NotifyNpcPluginsDiffer::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Username);
    PluginList::Write(aWriter, OnlyHere);
    PluginList::Write(aWriter, OnlyThere);
}

void NotifyNpcPluginsDiffer::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Username = Serialization::ReadString(aReader);
    PluginList::Read(aReader, OnlyHere);
    PluginList::Read(aReader, OnlyThere);
}
