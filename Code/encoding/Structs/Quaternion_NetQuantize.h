#pragma once

#include <glm/gtc/quaternion.hpp>

using TiltedPhoques::Buffer;

//! Network optimized quaternion, using the "smallest three" compression scheme.
//! The largest-magnitude component is dropped and rebuilt from the unit length; the other three get 10 bits each.
struct Quaternion_NetQuantize : glm::quat
{
    Quaternion_NetQuantize() = default;
    ~Quaternion_NetQuantize() = default;

    bool operator==(const Quaternion_NetQuantize& acRhs) const noexcept;
    bool operator!=(const Quaternion_NetQuantize& acRhs) const noexcept;

    Quaternion_NetQuantize& operator=(const glm::quat& acRhs) noexcept;

    void Serialize(Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Buffer::Reader& aReader) noexcept;

    [[nodiscard]] uint32_t Pack() const noexcept;
    void Unpack(uint32_t aValue) noexcept;
};
