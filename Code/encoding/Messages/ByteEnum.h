#pragma once

/** Enums a message carries in one byte: written and read as that whole byte. */
namespace ByteEnum
{
/** Bits an enum takes in a message. */
constexpr uint32_t kBits = 8;

/**
 * Write a one-byte enum.
 *
 * @param aWriter Where the message goes.
 * @param aValue The enum's value.
 */
template <class T> void Write(TiltedPhoques::Buffer::Writer& aWriter, const T aValue) noexcept
{
    aWriter.WriteBits(static_cast<uint64_t>(aValue), kBits);
}

/**
 * Read a one-byte enum.
 *
 * @param aReader Where the message comes from.
 * @return The enum's value.
 */
template <class T> T Read(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t value = 0;
    aReader.ReadBits(value, kBits);
    return static_cast<T>(value);
}
} // namespace ByteEnum
