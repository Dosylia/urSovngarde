#pragma once

#include "Message.h"

#include <glm/gtc/quaternion.hpp>

//! Another player's bow gained or lost its nocked arrow, or an arrow in the hand (a relayed ArrowNockedRequest); the receiver updates the copy.
struct NotifyArrowNocked final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyArrowNocked;

    NotifyArrowNocked()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyArrowNocked& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Nocked == acRhs.Nocked && InHand == acRhs.InHand && HandSide == acRhs.HandSide &&
               HandOffset == acRhs.HandOffset && HandRotation == acRhs.HandRotation;
    }

    //! Server id of the archer's character.
    uint32_t ServerId{};
    bool Nocked{};
    //! The arrow is held in a hand and not on the string (Nocked false): taken from the quiver, or ready after a shot.
    bool InHand{};
    //! The hand holding it, 0 left or 1 right (InHand).
    uint8_t HandSide{};
    //! Where the arrow model sits in the space of that third-person hand bone (InHand).
    glm::vec3 HandOffset{};
    glm::quat HandRotation{};
};
