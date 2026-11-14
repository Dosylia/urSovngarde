#include <Messages/RequestNpcPlugins.h>
#include <Structs/PluginList.h>

void RequestNpcPlugins::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    PluginList::Write(aWriter, Plugins);
}

void RequestNpcPlugins::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    PluginList::Read(aReader, Plugins);
}
