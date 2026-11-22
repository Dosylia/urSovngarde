#include <TiltedOnlinePCH.h>
#include <Interface/GFxValue.h>

namespace
{
// GFxValue::ObjectInterface's functions, called with the interface as this; the same ids as CommonLibSSE and CommonLibVR.
// Both games call them with this argument pattern: the interface, the value's object, then the rest.
using TObjectRelease = void(void* apInterface, GFxValue* apValue, void* apData);
using TGetMember = bool(void* apInterface, void* apData, const char* acpName, GFxValue* apOut, bool aIsDisplayObject);
using TSetText = bool(void* apInterface, void* apData, const char* acpText, bool aIsHtml);
} // namespace

/**
 * Give back the reference this value holds on an object or a string of the menu's Flash movie, so the movie can free it.
 * Plain numbers and booleans hold none.
 */
GFxValue::~GFxValue() noexcept
{
    if (!IsManaged() || !m_pObjectInterface || !m_pData)
        return;

    POINTER_SKYRIMSE(TObjectRelease, s_objectRelease, 82270, 80245);
    if (s_objectRelease.Get())
        s_objectRelease.Get()(m_pObjectInterface, this, m_pData);
}

/**
 * Tell whether the value holds one kind of value, whether the movie counts a reference to it or not.
 *
 * @param aKind The kind: kNumber, kString, kObject, kArray or kDisplayObject.
 * @return True when the value holds that kind.
 */
bool GFxValue::Holds(const Type aKind) const noexcept
{
    return GetKind() == aKind;
}

/**
 * Give the number the value holds.
 *
 * @return The number; nothing when the value holds no number.
 */
std::optional<double> GFxValue::GetNumber() const noexcept
{
    if (!Holds(Type::kNumber))
        return std::nullopt;
    return m_number;
}

/**
 * Make an empty value hold a number; a number holds no reference into the movie.
 *
 * @param aNumber The number.
 */
void GFxValue::SetNumber(const double aNumber) noexcept
{
    m_pObjectInterface = nullptr;
    m_type = static_cast<uint32_t>(Type::kNumber);
    m_number = aNumber;
}

/**
 * Give the string the value holds.
 *
 * @return The text; null when the value is no string.
 */
const char* GFxValue::GetString() const noexcept
{
    if (!Holds(Type::kString) || !m_pData)
        return nullptr;

    // A string the movie owns is a node whose first field is the text.
    return IsManaged() ? *static_cast<const char* const*>(m_pData) : static_cast<const char*>(m_pData);
}

/**
 * Read one member of the object this value holds.
 *
 * @param acpName The member's name.
 * @param aOut Where the member's value goes; it must be empty.
 * @return False when the value holds no object, or the movie has no such member.
 */
bool GFxValue::GetMember(const char* acpName, GFxValue& aOut) const noexcept
{
    POINTER_SKYRIMSE(TGetMember, s_getMember, 82245, 80222);
    if (!IsObject() || !m_pObjectInterface || !m_pData || !s_getMember.Get())
        return false;

    return s_getMember.Get()(m_pObjectInterface, m_pData, acpName, &aOut, Holds(Type::kDisplayObject));
}

/**
 * Set the text of the text field this value holds, as plain text.
 *
 * @param acpText The text.
 * @return False when the value holds no display object, or the game's function is not found.
 */
bool GFxValue::SetText(const char* acpText) noexcept
{
    POINTER_SKYRIMSE(TSetText, s_setText, 82293, 80270);
    if (!Holds(Type::kDisplayObject) || !m_pObjectInterface || !m_pData || !s_setText.Get())
        return false;

    return s_setText.Get()(m_pObjectInterface, m_pData, acpText, false);
}

/**
 * Give the kind of value held, without the managed bit.
 *
 * @return The kind, as a Type.
 */
GFxValue::Type GFxValue::GetKind() const noexcept
{
    return static_cast<Type>(m_type & kKindMask);
}

/**
 * Tell whether the value holds a counted reference into the movie, to give back when the value goes.
 *
 * @return True when the managed bit is set.
 */
bool GFxValue::IsManaged() const noexcept
{
    return (m_type & kManagedBit) != 0;
}

/**
 * Tell whether the value holds an object of any kind: an object, an array or a display object.
 *
 * @return True for those three kinds.
 */
bool GFxValue::IsObject() const noexcept
{
    const Type cKind = GetKind();
    return cKind == Type::kObject || cKind == Type::kArray || cKind == Type::kDisplayObject;
}
