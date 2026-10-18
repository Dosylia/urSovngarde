#pragma once

#include "Message.h"

#include <glm/gtc/quaternion.hpp>

//! The sender's bow gained or lost its nocked arrow, or an arrow in the hand; the server relays it as NotifyArrowNocked.
//! In VR that arrow is a game attachment made at attack state 10 (kBowAttached), which nothing else syncs.
struct ArrowNockedRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kArrowNockedRequest;

    ArrowNockedRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ArrowNockedRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Nocked == acRhs.Nocked && InHand == acRhs.InHand && HandSide == acRhs.HandSide &&
               HandOffset == acRhs.HandOffset && HandRotation == acRhs.HandRotation;
    }

    bool Nocked{};
    //! The arrow is held in a hand and not on the string (Nocked false): taken from the quiver, or ready after a shot.
    bool InHand{};
    //! The hand holding it, 0 left or 1 right (InHand).
    uint8_t HandSide{};
    //! Where the arrow model sits in the space of that third-person hand bone (InHand).
    glm::vec3 HandOffset{};
    glm::quat HandRotation{};
};
