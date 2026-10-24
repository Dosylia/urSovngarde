#include <Structs/ReferenceUpdate.h>
#include <TiltedCore/Serialization.hpp>
#include <stdexcept>

using TiltedPhoques::Serialization;

bool ReferenceUpdate::operator==(const ReferenceUpdate& acRhs) const noexcept
{
    return UpdatedMovement == acRhs.UpdatedMovement && UpdatedVRPose == acRhs.UpdatedVRPose;
}

bool ReferenceUpdate::operator!=(const ReferenceUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ReferenceUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    UpdatedMovement.Serialize(aWriter);
    UpdatedVRPose.Serialize(aWriter);
}

void ReferenceUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader)
{
    UpdatedMovement.Deserialize(aReader);
    UpdatedVRPose.Deserialize(aReader);
}
