#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VR/VRControllers.h>
#include <Helpers/FiniteVector.h>

namespace VRControllers
{
// The game's axes (x right, y ahead, z up) become the runtime's (x right, y up, -z ahead) in all four turns below.
/**
 * Turn a position in the game's world into where it is seen from the headset.
 *
 * @param acWorld A position in the game's world, in game units.
 * @return The same position relative to the headset, in metres, the way the VR runtime counts.
 */
glm::vec3 HeadSpace::Point(const glm::vec3& acWorld) const noexcept
{
    const glm::vec3 cGame = ToHead * (acWorld - Position) / UnitsPerMetre;
    return {cGame.x, cGame.z, -cGame.y};
}

/**
 * Turn a direction in the game's world into the same direction seen from the headset.
 *
 * @param acWorld A direction in the game's world.
 * @return The same direction relative to the headset, the way the VR runtime counts.
 */
glm::vec3 HeadSpace::Direction(const glm::vec3& acWorld) const noexcept
{
    const glm::vec3 cGame = ToHead * acWorld;
    return {cGame.x, cGame.z, -cGame.y};
}

/**
 * Turn a position seen from the headset back into the game's world.
 *
 * @param acHead A position relative to the headset, in metres.
 * @return The same position in the game's world, in game units.
 */
glm::vec3 HeadSpace::WorldPoint(const glm::vec3& acHead) const noexcept
{
    return Position + glm::transpose(ToHead) * glm::vec3(acHead.x, -acHead.z, acHead.y) * UnitsPerMetre;
}

/**
 * Turn a direction seen from the headset back into the game's world.
 *
 * @param acHead A direction relative to the headset.
 * @return The same direction in the game's world.
 */
glm::vec3 HeadSpace::WorldDirection(const glm::vec3& acHead) const noexcept
{
    return glm::transpose(ToHead) * glm::vec3(acHead.x, -acHead.z, acHead.y);
}

/**
 * Check if this headset space can be used: every number in it is ordinary.
 * A broken headset transform would otherwise send NaN into every position worked out from it.
 *
 * @return True when the position, the rotation and the units per metre are all ordinary numbers; a broken node scale already counts as 1.
 */
bool HeadSpace::IsUsable() const noexcept
{
    return IsFiniteVector(Position) && IsFiniteMatrix(ToHead) && std::isfinite(UnitsPerMetre);
}

/**
 * Find which way the player's head faces, in the game's world.
 *
 * @return A direction of length 1.
 */
glm::vec3 HeadSpace::Ahead() const noexcept
{
    return WorldDirection({0.f, 0.f, -1.f});
}

/**
 * Find which way is the player's right, in the game's world.
 *
 * @return A direction of length 1.
 */
glm::vec3 HeadSpace::Right() const noexcept
{
    return WorldDirection({1.f, 0.f, 0.f});
}

/**
 * Work out how far a point in the game's world is from where the player is looking.
 *
 * @param acWorld A position in the game's world.
 * @return The angle between straight ahead and the point, in radians: 0 straight ahead, pi straight behind.
 */
float HeadSpace::AngleFromGaze(const glm::vec3& acWorld) const noexcept
{
    const glm::vec3 cSeen = Point(acWorld);
    const float cDistance = glm::length(cSeen);
    if (!(cDistance > 0.f))
        return 0.f;
    // For the headset, straight ahead is -z.
    return std::acos(glm::clamp(-cSeen.z / cDistance, -1.f, 1.f));
}
} // namespace VRControllers

#ifdef SKYRIMVR

#include <Games/Skyrim/NetImmerse/NiTransform.h>
#include <Games/Skyrim/NetImmerse/NiGlm.h>
#include <Games/Skyrim/NetImmerse/NiLayout.h>
#include <PlayerCharacter.h>
#include <Helpers/ReadableMemory.h>

#include <openvr/openvr.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
using NiLayout::kNameOffset;
using NiLayout::kWorldOffset;

// PlayerCharacter's VR nodes (CommonLibVR's VR_NODE_DATA; the headset as measured by TiltedEvolutionVR), each checked by name before use.
constexpr uint32_t kHeadsetOffset = 0x570;
constexpr uint32_t kLeftWandOffset = 0x490;
constexpr uint32_t kRightWandOffset = 0x4F8;

// A device's kind is its input device type, and its place in BSInputDeviceManager's device list.
// 0 to 2 are the keyboard, the mouse and the gamepad.
// 3 to 8 are the VR controllers, one per headset maker and hand: Vive right and left, Oculus right and left, Windows Mixed Reality right and left.
// Primary, the right hand, comes first in each pair; the left-handed setting does not change which hand a device is.
constexpr int kFirstControllerKind = 3;
constexpr int kLastControllerKind = 8;
// Where BSInputDeviceManager keeps its device list: one device pointer per kind.
constexpr uint32_t kDeviceListOffset = 0x60;
// Where a VR controller device keeps its buttons and stick as the game last read them.
constexpr uint32_t kDeviceStateOffset = 0xC0;
// OpenVR packs a controller's buttons into one 64-bit number, one bit per button; each mask below holds the bits of the buttons it tests.
// The grip button, or Axis2, which Oculus Touch controllers report for the grip.
constexpr uint64_t kGripMask = (1ull << vr::k_EButton_Grip) | (1ull << vr::k_EButton_Axis2);
constexpr uint64_t kTriggerMask = 1ull << vr::k_EButton_SteamVR_Trigger;
constexpr uint64_t kStickClickMask = 1ull << vr::k_EButton_SteamVR_Touchpad;

/**
 * What is known about the node at one of PlayerCharacter's offsets on this game build.
 */
enum class NodeCheck : int
{
    kUnchecked, // not looked at yet
    kValid,     // carries the expected name
    kWrongNode, // holds another node on this build: never read again
};

/**
 * Check if a node carries this name, reading its memory only where it can be read.
 *
 * @param apNode The node to look at.
 * @param acpName The name it should carry, such as "HmdNode".
 * @return True when the node's name is exactly that.
 */
bool HasName(const uint8_t* apNode, const char* acpName) noexcept
{
    if (!ReadableMemory::IsReadable(apNode + kNameOffset, sizeof(const char*)))
        return false;
    const char* pName = *reinterpret_cast<const char* const*>(apNode + kNameOffset);
    if (!pName || !ReadableMemory::IsReadable(pName, std::strlen(acpName) + 1))
        return false;
    return std::strcmp(pName, acpName) == 0;
}

/**
 * Check that the node at one of PlayerCharacter's offsets is the one expected, and remember the answer.
 * A wrong node is reported once in the log.
 *
 * @param apNode The node found at that offset.
 * @param acpName The name it must carry.
 * @param aOffset Where it was found in PlayerCharacter, for the log line.
 * @param aCheck Where the answer is remembered.
 * @return True when it is the expected node.
 */
bool ConfirmNodeName(const uint8_t* apNode, const char* acpName, const uint32_t aOffset, std::atomic<NodeCheck>& aCheck) noexcept
{
    const bool cMatches = HasName(apNode, acpName);
    const NodeCheck cResult = cMatches ? NodeCheck::kValid : NodeCheck::kWrongNode;
    // Two threads may check at the same moment: only the first one to record the answer logs it.
    if (aCheck.exchange(cResult) == NodeCheck::kUnchecked && !cMatches)
        spdlog::warn("VRControllers: PlayerCharacter+0x{:X} is not the {} on this build", aOffset, acpName);
    return cMatches;
}

/**
 * Find where one of the player's VR nodes is in the world, such as the headset or a controller.
 * The first time, the node's name is checked, so a game build that keeps another node there is never misread.
 * Called from the game's update and from its render thread.
 *
 * @param aOffset Where the node's pointer sits in PlayerCharacter.
 * @param acpName The name the node must carry, such as "HmdNode".
 * @param aCheck What is known about that offset so far; updated by the first call.
 * @return The node's world transform, or nothing before the player or its VR body exists, or when the offset holds another node.
 */
const NiTransform* NodeTransform(const uint32_t aOffset, const char* acpName, std::atomic<NodeCheck>& aCheck) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || aCheck.load() == NodeCheck::kWrongNode)
        return nullptr;

    auto* pNode = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(pPlayer) + aOffset);
    if (!pNode)
        return nullptr;

    if (aCheck.load() == NodeCheck::kUnchecked && !ConfirmNodeName(pNode, acpName, aOffset, aCheck))
        return nullptr;
    return reinterpret_cast<const NiTransform*>(pNode + kWorldOffset);
}

/**
 * Find the game's input device manager, the object that owns every input device.
 *
 * @return The manager, or nothing when this game version lacks its address or the game has not made it yet.
 */
const uint8_t* InputDeviceManager() noexcept
{
    // The game keeps the manager's address in a global variable, found by its address ID.
    POINTER_SKYRIMSE(const uint8_t*, s_managerVariable, 516574, 516574);
    const uint8_t* const* pVariable = s_managerVariable.Get();
    if (!pVariable)
        return nullptr;

    // Read the variable: null until the game has made the manager.
    return *pVariable;
}

/**
 * Find the device in one slot of the game's input device list.
 *
 * @param apManager The input device manager.
 * @param aKind The slot to read: the device's kind.
 * @return The device, or null when that slot is empty.
 */
const uint8_t* DeviceOfKind(const uint8_t* apManager, const int aKind) noexcept
{
    const auto* pDeviceList = reinterpret_cast<const uint8_t* const*>(apManager + kDeviceListOffset);
    return pDeviceList[aKind];
}

/**
 * Check if an input device is one of the game's VR controllers.
 * Every game object starts with the address of its class's method table (its vtable).
 * So a device whose first value is the VR controller class's table is a VR controller.
 *
 * @param apDevice The device to look at.
 * @return True for a VR controller.
 */
bool IsControllerDevice(const uint8_t* apDevice) noexcept
{
    POINTER_SKYRIMSE(void, s_controllerMethodTable, 9000014, 9000014);
    const void* pControllerTable = s_controllerMethodTable.GetPtr();
    if (!apDevice || !pControllerTable)
        return false;

    const void* pDeviceTable = *reinterpret_cast<const void* const*>(apDevice);
    return pDeviceTable == pControllerTable;
}

/**
 * Visit each of the game's VR controller devices, with its kind: which headset maker's controller, and which hand.
 * Kinds come in pairs per headset maker: an odd kind is a right hand, an even kind a left hand.
 *
 * @param aVisit Called with the device's kind and its button state as the game last read it.
 */
template <class F> void ForEachControllerDevice(F&& aVisit) noexcept
{
    const uint8_t* pManager = InputDeviceManager();
    if (!pManager)
        return;

    // Go through the six controller slots: Vive, Oculus and Windows Mixed Reality, each right then left.
    for (int kind = kFirstControllerKind; kind <= kLastControllerKind; ++kind)
    {
        // Skip a slot that is empty or does not hold a VR controller.
        const uint8_t* pDevice = DeviceOfKind(pManager, kind);
        if (!IsControllerDevice(pDevice))
            continue;

        // Hand over what this controller's buttons and stick were doing when the game last read them.
        const auto& buttons = *reinterpret_cast<const vr::VRControllerState_t*>(pDevice + kDeviceStateOffset);
        aVisit(kind, buttons);
    }
}

/**
 * Find which hand a controller device is for, from its kind.
 * The first of each pair is the right hand.
 * The left-handed setting changes which hand the game uses for actions, not which hand a device is.
 *
 * @param aKind The device's kind, 3 to 8.
 * @return The hand that holds that controller.
 */
VRControllers::Hand HandOfKind(const int aKind) noexcept
{
    const bool cFirstOfPair = (aKind - kFirstControllerKind) % 2 == 0;
    return cFirstOfPair ? VRControllers::Hand::kRight : VRControllers::Hand::kLeft;
}

/**
 * Find the state of the controller the player really holds in one hand.
 * The game keeps a device for each controller family it supports (Vive, Oculus, Windows Mixed Reality) and each hand, all at once.
 * Only the family the player really holds gets fresh input, so the device whose state changed last (the highest packet number) is the one in use.
 *
 * @param aHand Which hand.
 * @return That controller's state, or nothing when the game has no controller device for that hand.
 */
std::optional<vr::VRControllerState_t> NewestStateOf(const VRControllers::Hand aHand) noexcept
{
    std::optional<vr::VRControllerState_t> newest;
    ForEachControllerDevice(
        [&](const int aKind, const vr::VRControllerState_t& acState)
        {
            if (HandOfKind(aKind) != aHand)
                return;
            if (!newest || acState.unPacketNum > newest->unPacketNum)
                newest = acState;
        });
    return newest;
}

/**
 * Read a controller's raw state as the buttons and stick this mod uses.
 *
 * @param acState The controller's state as the game last read it.
 * @return Its trigger, grip, stick click and stick position, marked as connected.
 */
VRControllers::Buttons ButtonsFrom(const vr::VRControllerState_t& acState) noexcept
{
    VRControllers::Buttons buttons;
    buttons.Connected = true;
    buttons.Trigger = (acState.ulButtonPressed & kTriggerMask) != 0;
    buttons.Grip = VRControllers::IsGripPressed(acState.ulButtonPressed);
    buttons.StickClick = (acState.ulButtonPressed & kStickClickMask) != 0;
    // The first axis is the thumbstick, or the touchpad on wands that have one.
    buttons.StickX = acState.rAxis[0].x;
    buttons.StickY = acState.rAxis[0].y;
    return buttons;
}

// Game units in a metre of the room, before the scale of the player's VR nodes.
constexpr float kGameUnitsPerMetre = 70.f;

/**
 * Work out how many game units make one metre of the player's room.
 * It is 70 times the scale of the player's VR nodes, the commonly used figure for Skyrim.
 * Measured on the test rig it is closer to 74.5, so things placed with it can land a little off; that difference is still open.
 *
 * @param acHead The headset's world transform; its scale is the VR nodes' scale.
 * @return The game units in one metre.
 */
float UnitsPerMetre(const NiTransform& acHead) noexcept
{
    // A missing scale counts as 1, so nothing is ever divided by zero.
    const float cScale = acHead.scale > 0.f ? acHead.scale : 1.f;
    return kGameUnitsPerMetre * cScale;
}

/**
 * Prepare what is needed to tell where things are relative to the player's head, such as "2 m straight ahead".
 * It keeps where the head is, which way it faces, and how many game units make a metre.
 *
 * @param acHead Where the game has the headset: its position, rotation and scale in the game's world.
 * @return The head's point of view, ready to turn world positions into head-relative ones and back.
 */
VRControllers::HeadSpace HeadSpaceFrom(const NiTransform& acHead) noexcept
{
    VRControllers::HeadSpace space;
    // The headset's rotation turns head directions into world ones; its transpose undoes that.
    space.ToHead = glm::transpose(ToGlm(acHead.rotate));
    space.Position = ToGlm(acHead.translate);
    space.UnitsPerMetre = UnitsPerMetre(acHead);
    return space;
}

/**
 * Write once per game how many game units make a metre, to check placement against measurements.
 *
 * @param acSpace The headset's space just built.
 * @param aNodeScale The scale of the player's VR nodes it was built with.
 */
void LogUnitsPerMetreOnce(const VRControllers::HeadSpace& acSpace, const float aNodeScale) noexcept
{
    static std::atomic<bool> s_logged = false;
    if (s_logged.exchange(true))
        return;
    spdlog::info("VRControllers: {:.1f} game units to the metre (headset node scale {:.3f})", acSpace.UnitsPerMetre, aNodeScale);
}

/**
 * Find where one of the player's controllers is in the game's world, and which way it points.
 * The game calls its controller nodes wands (LeftWandNode, RightWandNode).
 *
 * @param aHand Which hand's controller.
 * @return The controller's position, rotation and scale in game units, or nothing before the player's VR body exists or when this game build keeps another node there.
 */
const NiTransform* ControllerTransform(const VRControllers::Hand aHand) noexcept
{
    // Each node is checked on the first call; the answer is kept for the whole game.
    static std::atomic<NodeCheck> s_leftCheck = NodeCheck::kUnchecked;
    static std::atomic<NodeCheck> s_rightCheck = NodeCheck::kUnchecked;
    if (aHand == VRControllers::Hand::kLeft)
        return NodeTransform(kLeftWandOffset, "LeftWandNode", s_leftCheck);
    return NodeTransform(kRightWandOffset, "RightWandNode", s_rightCheck);
}
} // namespace

namespace VRControllers
{
/**
 * Find where the player's headset is in the game's world, and which way it faces.
 *
 * @return The headset's position, rotation and scale in game units, or nothing before the player's VR body exists or when this game build keeps another node there.
 */
const NiTransform* HeadsetTransform() noexcept
{
    // The node is checked on the first call; the answer is kept for the whole game.
    static std::atomic<NodeCheck> s_check = NodeCheck::kUnchecked;
    return NodeTransform(kHeadsetOffset, "HmdNode", s_check);
}

/**
 * Find the headset's space as the game holds it this frame, to place things relative to the player's head.
 *
 * @return The headset's space, or nothing before the player's VR body exists or when its transform holds broken numbers.
 */
std::optional<HeadSpace> HeadSpaceNow() noexcept
{
    const NiTransform* pHead = HeadsetTransform();
    if (!pHead)
        return std::nullopt;

    const HeadSpace space = HeadSpaceFrom(*pHead);
    if (!space.IsUsable())
        return std::nullopt;

    LogUnitsPerMetreOnce(space, pHead->scale);
    return space;
}

/**
 * Find where one of the player's controllers is and which way it points, like a laser pointer.
 *
 * @param aHand Which hand's controller.
 * @return Where it is and where it points, in the game's world; nothing before the player's VR body exists or when its numbers are broken.
 */
std::optional<Pointer> PointerOf(const Hand aHand) noexcept
{
    const NiTransform* pController = ControllerTransform(aHand);
    if (!pController)
        return std::nullopt;

    // The game's controllers point along their own y axis.
    const glm::vec3 from = ToGlm(pController->translate);
    const glm::vec3 along = ToGlm(pController->rotate) * glm::vec3(0.f, 1.f, 0.f);

    // Broken numbers, or a direction too short to tell where it points, point nowhere.
    if (!IsFiniteVector(from) || !IsFiniteVector(along) || !(glm::length2(along) > 1e-6f))
        return std::nullopt;

    return Pointer{from, glm::normalize(along)};
}

/**
 * Find what the buttons and stick of one hand's controller are doing, as the game last read them.
 *
 * @param aHand Which hand.
 * @return Its buttons and stick; marked not connected, with everything released, when the game has no controller for that hand.
 */
Buttons ButtonsOf(const Hand aHand) noexcept
{
    const auto state = NewestStateOf(aHand);
    if (!state)
        return {};
    return ButtonsFrom(*state);
}

/**
 * Count how many hands hold their controller's grip, as the game's own controller devices last read them.
 * Each hand counts once, even though the game keeps a device per controller family for each hand.
 *
 * @return 0, 1 or 2.
 */
uint32_t GripsHeld() noexcept
{
    uint32_t held = 0;
    for (const Hand cHand : {Hand::kLeft, Hand::kRight})
    {
        if (ButtonsOf(cHand).Grip)
            ++held;
    }
    return held;
}

/**
 * Check if a controller's pressed buttons include its grip.
 * Oculus Touch controllers report the grip as Axis2, the others as the grip button.
 *
 * @param aPressedButtons The controller's pressed buttons, one bit per button, as OpenVR reports them.
 * @return True when the grip is pressed.
 */
bool IsGripPressed(const uint64_t aPressedButtons) noexcept
{
    return (aPressedButtons & kGripMask) != 0;
}
} // namespace VRControllers

#else

// The flat game has no headset and no VR controllers: each answer is "none".
namespace VRControllers
{
/**
 * Find the headset in the game's world: the flat game has none.
 *
 * @return Always null.
 */
const NiTransform* HeadsetTransform() noexcept
{
    return nullptr;
}

/**
 * Find the headset's space: the flat game has no headset.
 *
 * @return Always nothing.
 */
std::optional<HeadSpace> HeadSpaceNow() noexcept
{
    return std::nullopt;
}

/**
 * Find where a controller points: the flat game has no VR controllers.
 *
 * @return Always nothing.
 */
std::optional<Pointer> PointerOf(Hand) noexcept
{
    return std::nullopt;
}

/**
 * Read a controller's buttons: the flat game has no VR controllers.
 *
 * @return Always marked not connected, with everything released.
 */
Buttons ButtonsOf(Hand) noexcept
{
    return {};
}

/**
 * Count the hands holding a grip: the flat game has no VR controllers.
 *
 * @return Always 0.
 */
uint32_t GripsHeld() noexcept
{
    return 0;
}

/**
 * Check a controller's grip: the flat game has no VR controllers.
 *
 * @return Always false.
 */
bool IsGripPressed(uint64_t) noexcept
{
    return false;
}
} // namespace VRControllers

#endif
