#pragma once

#include <Structs/VRPose.h>

struct Actor;
struct PlayerCharacter;

//! Syncs the pose of VR players: spine, head, arms and hands always.
//! Hips, thighs, calves and feet too when the sender runs a body-tracking plugin (SkyrimVR FBT); the receiver needs nothing installed.
//!
//! Local side: reads the root-relative bone rotations of the player's skeleton (driven by VRIK from the headset and controllers).
//! Remote side: once per frame, at the renderer's frame end, the remote skeleton is posed from the latest interpolated pose.
//! Writing from the animation job threads would let the renderer read half-written bones.
namespace VRBodySync
{
//! Renderer frame end (BSGraphics StopTimer). One thread, once per frame.
void OnFrameEnd() noexcept;

//! The other player's weapon (on their copy FormId, Side 0 left / 1 right) met one of this player's HIGGS bodies.
struct Clash
{
    uint32_t FormId = 0;
    uint8_t Side = 0;
    //! This player's hand that met it: 0 left, 1 right, 2 not known.
    uint8_t OwnSide = 2;
    glm::vec3 Point{};
    //! Game units a second.
    float Speed = 0.f;
    //! Whether its sound was played.
    bool Heard = false;
    //! True when the two started touching (felt, heard and sent).
    //! False when they are still touching (only weighed by the defender's rule).
    bool Start = true;
    //! How far the HIGGS body that met it was from that hand's node.
    float HandDistance = 0.f;
    //! What met it can parry: a weapon or shield in that hand, or something held up with HIGGS.
    //! A bare or spell hand cannot, so such a touch is not felt, heard, sent nor weighed by the defender's rule.
    bool Parries = false;
};
//! The next clash seen at a frame end, felt and heard as it is taken when it is a meeting; false when there is none.
bool TakeClash(Clash& aOut) noexcept;
//! A clash the other player saw on one of this player's weapons: felt on the hand holding it (0 left, 1 right).
void PulseClashHaptics(uint8_t aSide) noexcept;
//! A dead body whose owner has it elsewhere: its ragdoll, which it is drawn from, is moved there.
//! Every rigid body moves by the same offset, when it lies more than 64 units away (at most twice a second).
//! False when nothing was moved.
bool PlaceCorpse(Actor* apActor, const glm::vec3& acWanted) noexcept;
//! The names of the nodes in an actor's 3D whose name contains `acpPart` (any case), comma separated, at most `aMax`.
//! How many there were in all goes to `aCount`. For checks that cannot look at the screen (the rig).
std::string NodesNamedLike(Actor* apActor, const char* acpPart, size_t aMax, size_t& aCount) noexcept;
//! This copy has an arrow on its bow string (NotifyArrowNocked).
//! The arrow is laid from the string hand to the bow hand each frame, instead of where the game's flat attachment puts it.
void SetArrowNocked(uint32_t aFormId, bool aNocked) noexcept;
//! Whether this player's HIGGS bodies, or what they hold, touched another player's weapon body at that point in the last half second.
//! A hit there landed on the weapon, not on its owner.
bool IsWeaponTouchAt(const glm::vec3& acPoint) noexcept;
//! Whether one of this player's blades rests on a blade of that copy (the blade stop), now or in the last 300 ms.
//! The hit point must also lie along where the hand holds that blade; such a blade never reached the other player.
//! With no point (zero or not a number), any blade resting on his counts.
bool IsBladeStoppedAt(uint32_t aCopyFormId, const glm::vec3& acPoint) noexcept;
//! The game's sparks of a blade on metal, at a clash's point, for a second; false when none were made.
bool PlayClashSparks(const glm::vec3& acPoint) noexcept;
//! The game's blade-block sound, at a clash's point; false when it could not be played.
bool PlayClashSound(const glm::vec3& acPoint) noexcept;

bool CaptureLocalPose(PlayerCharacter* apPlayer, VRPose& aOutPose) noexcept;



//! The pose of a dead NPC this machine owns, so the other players see it slump and get dragged as it does here.
//! Upper body, and the legs when the skeleton has them.
//! Returns false, leaving the pose empty, when the body has not moved since the last send.
bool CaptureBodyPose(Actor* apActor, VRPose& aOutPose) noexcept;
//! Watch a dead body this client does not own for signs of being handled here.
//! True once per grab, after a third of a second of motion; the caller then asks for the body so that this side sends it.
//! acOwnerPosition is where the owner's updates put it: motion that follows those is the owner's, not a hand here.
bool IsRemoteBodyGrabbedHere(Actor* apActor, const glm::vec3& acOwnerPosition) noexcept;

//! Weapon touch: a controller pulse when your blade or your hand meets somebody else's weapon.
//!
//! Driven from the game thread once per frame, around the loop that already walks every tracked actor.
//! Call BeginWeaponTouch, then ConsiderForWeaponTouch for each actor, then EndWeaponTouch.
//! Works against NPCs and other players' copies alike, since the copy's weapon is a node like any other.
void BeginWeaponTouch() noexcept;
void ConsiderForWeaponTouch(Actor* apActor) noexcept;
void EndWeaponTouch() noexcept;

//! A pose without data clears the actor's pose.
void SetRemotePose(Actor* apActor, const VRPose& acPose) noexcept;
void ClearRemotePose(uint32_t aFormId) noexcept;

//! A cheap fingerprint of the local transforms of the first nodes below an actor's 3D root; 0 when the actor has no 3D.
//! Equal fingerprints across frames on a moving body mean its animation graph is not advancing.
uint64_t SkeletonMotionFingerprint(Actor* apActor) noexcept;
//! Angle in degrees between where the headset points and a world position, to tell whether the player is looking at it.
//! 0 is dead centre; -1 when the headset node cannot be read on this build.
float HeadsetAngleTo(const NiPoint3& acPosition) noexcept;

//! Where the headset is in the world and where it faces, for placing other players' voices.
//! False when the headset node cannot be read on this build.
bool HeadsetPose(glm::vec3& aPosition, glm::vec3& aForward, glm::vec3& aRight) noexcept;

//! TEMPORARY: one line about an actor's 3D for the invisible-copy diagnosis.
//! It lists the root, its children, and the skeleton root's world scale and position.
std::string DescribeBody(Actor* apActor) noexcept;
} // namespace VRBodySync
