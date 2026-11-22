#pragma once

#include <optional>

/**
 * The game's Scaleform value (Scaleform::GFx::Value): what a menu's Flash movie and the game hand each other.
 * It holds a number, a string, or an ActionScript object or display object (a movie clip, a text field) of the movie.
 * The same eight bytes hold a number or a pointer, as the type field says, so every read checks the type first.
 * It covers numbers, strings, reading a member and setting a text field's text; more of the game's functions go here as other menus need them.
 */
struct GFxValue
{
    /** The kind of value held, in the type field's low bits. */
    enum class Type : uint32_t
    {
        kUndefined = 0x00,
        kNumber = 0x03,
        kString = 0x04,
        kObject = 0x06,
        kArray = 0x07,
        kDisplayObject = 0x08,
    };

    /** The type field's bit that says the value holds a counted reference into the movie, given back when the value goes. */
    static constexpr uint32_t kManagedBit = 1u << 6;

    /** The type field's bits that make the kind, with the convert bit, as the game masks them. */
    static constexpr uint32_t kKindMask = 0x8F;

    /**
     * Make an empty value, for the movie to fill.
     */
    GFxValue() noexcept = default;

    /**
     * Give back the reference this value holds on an object or a string of the menu's Flash movie, so the movie can free it.
     * Plain numbers and booleans hold none.
     */
    ~GFxValue() noexcept;

    GFxValue(const GFxValue&) = delete;
    GFxValue& operator=(const GFxValue&) = delete;

    /**
     * Tell whether the value holds one kind of value, whether the movie counts a reference to it or not.
     *
     * @param aKind The kind: kNumber, kString, kObject, kArray or kDisplayObject.
     * @return True when the value holds that kind.
     */
    bool Holds(Type aKind) const noexcept;

    /**
     * Give the number the value holds.
     *
     * @return The number; nothing when the value holds no number.
     */
    std::optional<double> GetNumber() const noexcept;

    /**
     * Make an empty value hold a number; a number holds no reference into the movie.
     *
     * @param aNumber The number.
     */
    void SetNumber(double aNumber) noexcept;

    /**
     * Give the string the value holds.
     *
     * @return The text; null when the value is no string.
     */
    const char* GetString() const noexcept;

    /**
     * Read one member of the object this value holds.
     *
     * @param acpName The member's name.
     * @param aOut Where the member's value goes; it must be empty.
     * @return False when the value holds no object, or the movie has no such member.
     */
    bool GetMember(const char* acpName, GFxValue& aOut) const noexcept;

    /**
     * Set the text of the text field this value holds, as plain text.
     *
     * @param acpText The text.
     * @return False when the value holds no display object, or the game's function is not found.
     */
    bool SetText(const char* acpText) noexcept;

private:
    /**
     * Give the kind of value held, without the managed bit.
     *
     * @return The kind, as a Type.
     */
    Type GetKind() const noexcept;

    /**
     * Tell whether the value holds a counted reference into the movie, to give back when the value goes.
     *
     * @return True when the managed bit is set.
     */
    bool IsManaged() const noexcept;

    /**
     * Tell whether the value holds an object of any kind: an object, an array or a display object.
     *
     * @return True for those three kinds.
     */
    bool IsObject() const noexcept;

    void* m_pObjectInterface{nullptr}; // +00, the movie's object interface, through which a counted reference is read and given back
    uint32_t m_type{0};                // +08, a Type
    uint32_t m_pad{0};
    union
    {
        void* m_pData{nullptr}; // +10, the object or string; a managed string points at a node whose first field is its text
        double m_number;        // +10, for a number
    };
};
static_assert(sizeof(GFxValue) == 0x18);
