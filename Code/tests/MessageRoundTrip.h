#pragma once

#include <TiltedCore/Buffer.hpp>

/**
 * Write a message into a buffer and read it back, as the other side of the connection would.
 *
 * @param acMessage The message to send.
 * @return The message as the receiver reads it.
 */
template <class T> T RoundTrip(const T& acMessage)
{
    // Room for every test message, names included.
    constexpr std::size_t kBufferBytes = 512;

    TiltedPhoques::Buffer buffer(kBufferBytes);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    acMessage.SerializeRaw(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    T back{};
    back.DeserializeRaw(reader);
    return back;
}
