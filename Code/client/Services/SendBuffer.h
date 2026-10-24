#pragma once

#include <Messages/Message.h>

#include <TiltedCore/Buffer.hpp>

//! How a client message is written before it is sent: one byte the packet layer uses, then the message.
namespace SendBuffer
{
// Each message is written into a buffer taken from a per-thread scratch area, reset after every send.
// The largest is the connect request, which carries every plugin's name: about 80 KB for a list of 2000 plugins.
// A buffer too small cuts the message short without any error, and the server then reads a list missing its end.
constexpr size_t kScratchBytes = 1 << 22;
constexpr size_t kBufferBytes = 1 << 20;

//! False when the message did not fit in the buffer: what it holds may be cut short and must not be sent.
inline bool Write(const ClientMessage& acMessage, TiltedPhoques::Buffer& aBuffer, TiltedPhoques::Buffer::Writer& aWriter) noexcept
{
    // The writer grows a full buffer, and a write is dropped only when it cannot, without a word to the message.
    // So a buffer that never had to grow holds the whole message, and one that grew may have lost its last fields.
    const size_t cSizeBefore = aBuffer.GetSize();
    aWriter.WriteBits(0, 8);
    acMessage.Serialize(aWriter);
    return aBuffer.GetSize() == cSizeBefore;
}
} // namespace SendBuffer
