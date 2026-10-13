#pragma once

#include <array>

#include <Structs/Quaternion_NetQuantize.h>

using TiltedPhoques::Buffer;

//! Pose of a VR player, synced with Movement: the upper body always, the legs when trackers drive them.
//! Bone rotations are relative to the actor's 3D root, so no headset or controller calibration is needed.
//! HasData is false, and nothing else is written, for non-VR actors.
//! HasLegs is true only with body trackers (SkyrimVR FBT); otherwise the walk animation drives the legs.
struct VRPose
{
    //! Bone order is parent-before-child; see VRBodySync.cpp for the node names.
    enum Bone : uint8_t
    {
        kSpine1,
        kSpine2,
        kNeck,
        kHead,
        kLeftClavicle,
        kLeftUpperArm,
        kLeftForearm,
        kLeftHand,
        kRightClavicle,
        kRightUpperArm,
        kRightForearm,
        kRightHand,
        kUpperBoneCount,
        // Lower body, present only when HasLegs.
        kPelvis = kUpperBoneCount,
        kLeftThigh,
        kLeftCalf,
        kLeftFoot,
        kRightThigh,
        kRightCalf,
        kRightFoot,
        kBoneCount
    };

    VRPose() = default;
    ~VRPose() = default;

    bool operator==(const VRPose& acRhs) const noexcept;
    bool operator!=(const VRPose& acRhs) const noexcept;

    void Serialize(Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Buffer::Reader& aReader) noexcept;

    bool HasData{false};

    //! No bones, only RootPosition: for a body without a humanoid rig (a Dwarven sphere, a spider).
    //! The receiver shifts the whole node tree instead of posing named bones.
    bool NoBones{false};

    bool HasLegs{false}; // the entries from kPelvis on are valid
    std::array<Quaternion_NetQuantize, kBoneCount> Bones{};

    //! Finger bones relative to their parent, left hand then right, five fingers of three bones from the thumb.
    //! Sent on change or once a second (HasFingers); the receiver keeps the last set.
    static constexpr size_t kFingersPerHand = 5;
    static constexpr size_t kBonesPerFinger = 3;
    static constexpr size_t kFingerBoneCount = 2 * kFingersPerHand * kBonesPerFinger;
    bool HasFingers{false};
    std::array<Quaternion_NetQuantize, kFingerBoneCount> Fingers{};

    //! Body size: the world scale of the skeleton root ("NPC Root [Root]") times 1000, VRIK's scale included.
    //! Sent on change or once a second (HasScale); the receiver keeps the last value.
    bool HasScale{false};
    uint16_t RootScale{1000};

    //! World position of the 3D root, sent only for a dead body being moved (HasRootPosition).
    //! A corpse follows its ragdoll, so the receiver writes this at frame end, like the bones.
    bool HasRootPosition{false};
    float RootPosition[3]{};

    //! Where the hips sit relative to the 3D root, sent while the legs are tracked (HasHips).
    //! Crouching and leaning change no bone rotation, so this moves the whole body; the spine does not hang off the pelvis.
    bool HasHips{false};
    float HipOffset[3]{};

    //! The sender's own hand positions relative to its 3D root, about once a second (HasHandCheck).
    //! For measurement only: the receiver compares them with its copy's hands.
    bool HasHandCheck{false};
    float LeftHandOffset[3]{};
    float RightHandOffset[3]{};

    //! Weapon placement, left then right, in the space of the third-person hand bone (HasWeapons).
    //! VR holds weapons at their own angle, so the copy places its blades from this instead of the skeleton.
    //! Sent on change or once a second; nothing is placed on a side whose WeaponHeld is false.
    bool HasWeapons{false};
    std::array<bool, 2> WeaponHeld{};
    std::array<Quaternion_NetQuantize, 2> WeaponRotation{};
    float WeaponOffset[2][3]{};
};
