#pragma once

#include <Structs/Movement.h>
#include <Structs/VRPose.h>

using TiltedPhoques::Buffer;
using TiltedPhoques::Vector;

//! One actor's snapshot in a movement message, sent unreliably: a lost one costs only itself.
//! Its actions travel on their own, reliably (RequestActionEvents, NotifyActionEvents).
struct ReferenceUpdate
{
    ReferenceUpdate() = default;
    ~ReferenceUpdate() = default;

    bool operator==(const ReferenceUpdate& acRhs) const noexcept;
    bool operator!=(const ReferenceUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader);

    Movement UpdatedMovement{};
    VRPose UpdatedVRPose{};
};
