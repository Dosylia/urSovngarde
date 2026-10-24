#pragma once

#include <Messages/ClientMessageFactory.h>
#include <DecodeGuard.h>

static std::function<UniquePtr<ClientMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_clientMessageExtractor[kClientOpcodeMax];

namespace details
{
static struct ClientMessageFactoryInit
{
    ClientMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_clientMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
            {
                auto ptr = TiltedPhoques::MakeUnique<T>();
                // A message a reader found damaged is dropped whole (DecodeGuard).
                DecodeGuard::t_failed = false;
                ptr->DeserializeRaw(aReader);
                if (DecodeGuard::t_failed)
                    return UniquePtr<ClientMessage>{nullptr};
                return TiltedPhoques::CastUnique<ClientMessage>(std::move(ptr));
            };

            return false;
        };

        ClientMessageFactory::Visit(extractor);
    }
} s_ClientMessageFactoryInit;
} // namespace details

UniquePtr<ClientMessage> ClientMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept
{
    uint64_t data;
    aReader.ReadBits(data, sizeof(ClientOpcode) * 8);

    if (data >= kClientOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<ClientOpcode>(data);
    return s_clientMessageExtractor[opcode](aReader);
}
