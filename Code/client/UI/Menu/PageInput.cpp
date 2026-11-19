#include <TiltedOnlinePCH.h>

#include <UI/Menu/PageInput.h>

namespace
{
/**
 * Name a mouse button the way the page's browser does.
 *
 * @param aButton Which button.
 * @return The browser's name for it.
 */
cef_mouse_button_type_t BrowserButtonOf(const PageInput::Button aButton) noexcept
{
    return aButton == PageInput::Button::kRight ? MBT_RIGHT : MBT_LEFT;
}

/**
 * Press an editing key on the page, as the page's fields expect it: down, then up.
 * Enter also types its character, which is what submits a form.
 *
 * @param apApp The page's browser.
 * @param aKey The key: backspace or Enter.
 */
void PressEditingKey(TiltedPhoques::OverlayApp* apApp, const uint16_t aKey) noexcept
{
    apApp->InjectKey(KEYEVENT_RAWKEYDOWN, 0, aKey, 0);
    if (aKey == VK_RETURN)
        apApp->InjectKey(KEYEVENT_CHAR, 0, L'\r', 0);
    apApp->InjectKey(KEYEVENT_KEYUP, 0, aKey, 0);
}
} // namespace

namespace PageInput
{
/**
 * Move the page's mouse pointer to a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 */
void MouseMove(TiltedPhoques::OverlayApp* apApp, const uint16_t aX, const uint16_t aY) noexcept
{
    if (apApp)
        apApp->InjectMouseMove(aX, aY, 0);
}

/**
 * Press a mouse button on the page, at a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 * @param aButton Which button.
 */
void MouseDown(TiltedPhoques::OverlayApp* apApp, const uint16_t aX, const uint16_t aY, const Button aButton) noexcept
{
    if (apApp)
        apApp->InjectMouseButton(aX, aY, BrowserButtonOf(aButton), false, 0);
}

/**
 * Let go of a mouse button on the page, at a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 * @param aButton Which button.
 */
void MouseUp(TiltedPhoques::OverlayApp* apApp, const uint16_t aX, const uint16_t aY, const Button aButton) noexcept
{
    if (apApp)
        apApp->InjectMouseButton(aX, aY, BrowserButtonOf(aButton), true, 0);
}

/**
 * Scroll the page with the mouse wheel, at a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 * @param aDelta How far, in wheel units: kWheelNotch is one notch, positive scrolls up.
 */
void MouseWheel(TiltedPhoques::OverlayApp* apApp, const uint16_t aX, const uint16_t aY, const int16_t aDelta) noexcept
{
    if (apApp)
        apApp->InjectMouseWheel(aX, aY, aDelta, 0);
}

/**
 * Type text into the page, where its caret is.
 * Backspace and Enter become real key presses, as the page's fields expect.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param acpUtf8 The typed characters, in UTF-8.
 * @param aLength How many bytes of them.
 */
void Text(TiltedPhoques::OverlayApp* apApp, const char* acpUtf8, const size_t aLength) noexcept
{
    if (!apApp || !acpUtf8 || aLength == 0)
        return;

    // The page takes characters as UTF-16.
    std::wstring text(aLength + 1, L'\0');
    const int count = MultiByteToWideChar(CP_UTF8, 0, acpUtf8, static_cast<int>(aLength), text.data(), static_cast<int>(text.size()));

    for (int i = 0; i < count && text[i]; ++i)
    {
        const wchar_t character = text[i];
        if (character == L'\b')
            PressEditingKey(apApp, VK_BACK);
        else if (character == L'\n' || character == L'\r')
            PressEditingKey(apApp, VK_RETURN);
        else
            apApp->InjectKey(KEYEVENT_CHAR, 0, static_cast<uint16_t>(character), 0);
    }
}
} // namespace PageInput
