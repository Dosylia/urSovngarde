#pragma once

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

struct NiTransform;

/**
 * The player's headset and controllers as the game itself sees them: its own nodes, and its own controller devices.
 * So they work on any VR runtime, and the test rig's controllers count too (DevBench answers the game's own calls).
 */
namespace VRControllers
{
enum class Hand : uint8_t
{
    kLeft,
    kRight,
};

struct Buttons
{
    bool Connected = false;
    bool Trigger = false;
    bool Grip = false;
    // The thumbstick pressed in, or the touchpad on wands that have one.
    bool StickClick = false;
    // The thumbstick, or the touchpad on wands that have one: -1 to 1, up and right positive.
    float StickX = 0.f;
    float StickY = 0.f;
};

/**
 * Find where the player's headset is in the game's world, and which way it faces.
 *
 * @return The headset's position, rotation and scale in game units, or nothing before the player's VR body exists or when this game build keeps another node there.
 */
const NiTransform* HeadsetTransform() noexcept;

/**
 * The player's head as a point of view: turns positions and directions between the game's world and the headset.
 * The game's world counts in game units, with x right, y ahead and z up.
 * The headset counts in metres, with x right, y up and -z ahead, the way the VR runtime does.
 */
struct HeadSpace
{
    // Turns a direction in the game's world into the same direction relative to the headset.
    glm::mat3 ToHead{1.f};
    // Where the headset is in the game's world, in game units.
    glm::vec3 Position{0.f};
    // How many game units make one metre of the player's room.
    float UnitsPerMetre = 1.f;

    /**
     * Turn a position in the game's world into where it is seen from the headset.
     *
     * @param acWorld A position in the game's world, in game units.
     * @return The same position relative to the headset, in metres, the way the VR runtime counts.
     */
    glm::vec3 Point(const glm::vec3& acWorld) const noexcept;
    /**
     * Turn a direction in the game's world into the same direction seen from the headset.
     *
     * @param acWorld A direction in the game's world.
     * @return The same direction relative to the headset, the way the VR runtime counts.
     */
    glm::vec3 Direction(const glm::vec3& acWorld) const noexcept;
    /**
     * Turn a position seen from the headset back into the game's world.
     *
     * @param acHead A position relative to the headset, in metres.
     * @return The same position in the game's world, in game units.
     */
    glm::vec3 WorldPoint(const glm::vec3& acHead) const noexcept;
    /**
     * Turn a direction seen from the headset back into the game's world.
     *
     * @param acHead A direction relative to the headset.
     * @return The same direction in the game's world.
     */
    glm::vec3 WorldDirection(const glm::vec3& acHead) const noexcept;
    /**
     * Check if this headset space can be used: every number in it is ordinary.
     * A broken headset transform would otherwise send NaN into every position worked out from it.
     *
     * @return True when the position, the rotation and the units per metre are all ordinary numbers; a broken node scale already counts as 1.
     */
    bool IsUsable() const noexcept;
    /**
     * Find which way the player's head faces, in the game's world.
     *
     * @return A direction of length 1.
     */
    glm::vec3 Ahead() const noexcept;
    /**
     * Find which way is the player's right, in the game's world.
     *
     * @return A direction of length 1.
     */
    glm::vec3 Right() const noexcept;
    /**
     * Work out how far a point in the game's world is from where the player is looking.
     *
     * @param acWorld A position in the game's world.
     * @return The angle between straight ahead and the point, in radians: 0 straight ahead, pi straight behind.
     */
    float AngleFromGaze(const glm::vec3& acWorld) const noexcept;
};

/**
 * Find the headset's space as the game holds it this frame, to place things relative to the player's head.
 *
 * @return The headset's space, or nothing before the player's VR body exists or when its transform holds broken numbers.
 */
std::optional<HeadSpace> HeadSpaceNow() noexcept;

/**
 * A controller held like a laser pointer: where it is, and which way it points.
 */
struct Pointer
{
    // Where the controller is in the game's world, in game units.
    glm::vec3 From{0.f};
    // Which way it points in the game's world, as a direction of length 1.
    glm::vec3 Along{0.f, 1.f, 0.f};
};

/**
 * Find where one of the player's controllers is and which way it points, like a laser pointer.
 *
 * @param aHand Which hand's controller.
 * @return Where it is and where it points, in the game's world; nothing before the player's VR body exists or when its numbers are broken.
 */
std::optional<Pointer> PointerOf(Hand aHand) noexcept;

/**
 * Find what the buttons and stick of one hand's controller are doing, as the game last read them.
 *
 * @param aHand Which hand.
 * @return Its buttons and stick; marked not connected, with everything released, when the game has no controller for that hand.
 */
Buttons ButtonsOf(Hand aHand) noexcept;

/**
 * Count how many hands hold their controller's grip, as the game's own controller devices last read them.
 * Each hand counts once, even though the game keeps a device per controller family for each hand.
 *
 * @return 0, 1 or 2.
 */
uint32_t GripsHeld() noexcept;

/**
 * Check if a controller's pressed buttons include its grip.
 * Oculus Touch controllers report the grip as Axis2, the others as the grip button.
 *
 * @param aPressedButtons The controller's pressed buttons, one bit per button, as OpenVR reports them.
 * @return True when the grip is pressed.
 */
bool IsGripPressed(uint64_t aPressedButtons) noexcept;
} // namespace VRControllers
