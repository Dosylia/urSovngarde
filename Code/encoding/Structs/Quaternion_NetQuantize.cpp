#include <Structs/Quaternion_NetQuantize.h>
#include <TiltedCore/Math.hpp>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

// 10 bits per component in [-1/sqrt(2), 1/sqrt(2)]; the dropped largest component is always >= 1/sqrt(2).
constexpr float cMaxComponent = 0.70710678f; // 1/sqrt(2)
constexpr uint32_t cComponentBits = 10;
constexpr uint32_t cComponentMax = (1u << cComponentBits) - 1;
constexpr float cScalingFactor = float(cComponentMax) / (2.0f * cMaxComponent);

bool Quaternion_NetQuantize::operator==(const Quaternion_NetQuantize& acRhs) const noexcept
{
    return Pack() == acRhs.Pack();
}

bool Quaternion_NetQuantize::operator!=(const Quaternion_NetQuantize& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

Quaternion_NetQuantize& Quaternion_NetQuantize::operator=(const glm::quat& acRhs) noexcept
{
    glm::quat::operator=(acRhs);
    return *this;
}

void Quaternion_NetQuantize::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Pack(), 32);
}

void Quaternion_NetQuantize::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t data = 0;
    aReader.ReadBits(data, 32);

    Unpack(static_cast<uint32_t>(data & 0xFFFFFFFF));
}

namespace
{
//! Quantise a quaternion, having already been told which component to drop.
uint32_t PackWithLargest(const glm::quat& acQuat, const uint32_t aLargestIndex) noexcept
{
    float components[4] = {acQuat.x, acQuat.y, acQuat.z, acQuat.w};

    // Negate so the dropped component is positive; q and -q are the same rotation.
    if (components[aLargestIndex] < 0.f)
    {
        for (auto& c : components)
            c = -c;
    }

    uint32_t data = aLargestIndex & 0x3;
    uint32_t shift = 2;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (i == aLargestIndex)
            continue;

        const float clamped = TiltedPhoques::Clamp(components[i], -cMaxComponent, cMaxComponent);

        // Round to the nearest bucket; truncating loses a bucket on every round trip (75 decodes as 74.9999847).
        const float scaled = (clamped + cMaxComponent) * cScalingFactor + 0.5f;
        const auto quantized = static_cast<uint32_t>(TiltedPhoques::Clamp(scaled, 0.f, float(cComponentMax))) & cComponentMax;
        data |= quantized << shift;
        shift += cComponentBits;
    }

    return data;
}

uint32_t LargestIndexOf(const glm::quat& acQuat) noexcept
{
    const float components[4] = {acQuat.x, acQuat.y, acQuat.z, acQuat.w};
    uint32_t largest = 0;
    float largestValue = std::abs(components[0]);
    for (uint32_t i = 1; i < 4; ++i)
    {
        if (std::abs(components[i]) > largestValue)
        {
            largestValue = std::abs(components[i]);
            largest = i;
        }
    }
    return largest;
}
} // namespace

uint32_t Quaternion_NetQuantize::Pack() const noexcept
{
    const glm::quat q = glm::normalize(static_cast<glm::quat>(*this));

    uint32_t data = PackWithLargest(q, LargestIndexOf(q));

    // Settle on an encoding that survives its own round trip, since equality compares packed forms.
    // Near a tie between two components, decoding can pick another largest one and pack to other bits.
    // So walk the encodings until one decodes to the component it dropped, or take the lowest of the cycle.
    constexpr size_t cMaxCycle = 8;
    uint32_t seen[cMaxCycle] = {};
    size_t count = 0;

    while (count < cMaxCycle)
    {
        Quaternion_NetQuantize decoded;
        decoded.Unpack(data);

        const uint32_t settledIndex = LargestIndexOf(decoded);
        if (settledIndex == (data & 0x3))
            return data;

        bool alreadySeen = false;
        for (size_t i = 0; i < count; ++i)
            alreadySeen = alreadySeen || seen[i] == data;

        if (alreadySeen)
            break;

        seen[count++] = data;
        data = PackWithLargest(glm::normalize(static_cast<glm::quat>(decoded)), settledIndex);
    }

    uint32_t lowest = data;
    for (size_t i = 0; i < count; ++i)
    {
        if (seen[i] < lowest)
            lowest = seen[i];
    }

    return lowest;
}

void Quaternion_NetQuantize::Unpack(uint32_t aValue) noexcept
{
    const uint32_t largestIndex = aValue & 0x3;

    float components[4] = {};
    uint32_t shift = 2;
    float sumOfSquares = 0.f;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (i == largestIndex)
            continue;

        const uint32_t quantized = (aValue >> shift) & cComponentMax;
        shift += cComponentBits;

        const float value = (static_cast<float>(quantized) / cScalingFactor) - cMaxComponent;
        components[i] = value;
        sumOfSquares += value * value;
    }

    components[largestIndex] = std::sqrt(TiltedPhoques::Max(0.f, 1.0f - sumOfSquares));

    x = components[0];
    y = components[1];
    z = components[2];
    w = components[3];
}
