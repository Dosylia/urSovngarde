#include <TiltedOnlinePCH.h>

#include <Services/InputService.h>
#include <Services/OverlayService.h>
#include <Services/MenuService.h>

#include <OverlayApp.hpp>

#include <DInputHook.hpp>
#include <WindowsHook.hpp>

#include <include/internal/cef_types.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>
#include <World.h>

#include "Games/Skyrim/Interface/MenuControls.h"

static OverlayService* s_pOverlay = nullptr;
static UINT s_currentACP = CP_ACP;

/**
 * Find the page's browser, once the overlay, its browser and its drawing are all up: the page takes input only then.
 *
 * @param apOverlay The overlay service, or null before the service starts or after it stops.
 * @return The page's browser, or null while any of them is missing.
 */
static OverlayApp* ReadyOverlayApp(OverlayService* apOverlay) noexcept
{
    if (!apOverlay)
        return nullptr;
    OverlayApp* pApp = apOverlay->GetOverlayApp();
    const auto pClient = pApp ? pApp->GetClient() : nullptr;
    if (!pClient || !pClient->GetOverlayRenderHandler())
        return nullptr;
    return pApp;
}

void ForceKillAllInput()
{
    MenuControls::GetInstance()->SetToggle(false);
}

uint32_t GetCefModifiers(uint16_t aVirtualKey)
{
    uint32_t modifiers = EVENTFLAG_NONE;

    if (GetKeyState(VK_MENU) & 0x8000)
    {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }

    if (GetKeyState(VK_CONTROL) & 0x8000)
    {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }

    if (GetKeyState(VK_SHIFT) & 0x8000)
    {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }

    if (GetKeyState(VK_LBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_RBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_MBUTTON) & 0x8000)
    {
        modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }

    if (GetKeyState(VK_CAPITAL) & 1)
    {
        modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    }

    if (GetKeyState(VK_NUMLOCK) & 1)
    {
        modifiers |= EVENTFLAG_NUM_LOCK_ON;
    }

    if (aVirtualKey)
    {
        if (aVirtualKey == VK_RCONTROL || aVirtualKey == VK_RMENU || aVirtualKey == VK_RSHIFT)
        {
            modifiers |= EVENTFLAG_IS_RIGHT;
        }
        else if (aVirtualKey == VK_LCONTROL || aVirtualKey == VK_LMENU || aVirtualKey == VK_LSHIFT)
        {
            modifiers |= EVENTFLAG_IS_LEFT;
        }
        else if (aVirtualKey >= VK_NUMPAD0 && aVirtualKey <= VK_DIVIDE)
        {
            modifiers |= EVENTFLAG_IS_KEY_PAD;
        }
    }

    return modifiers;
}

// remember to update this when updating toggle keys
bool IsToggleKey(int aKey) noexcept
{
    return aKey == VK_RCONTROL || aKey == VK_F2;
}

bool IsDisableKey(int aKey) noexcept
{
    return aKey == VK_ESCAPE;
}

/**
 * Handle a key on the flat screen: the menu key opens or closes the menu, and while the menu is open the page gets the key.
 *
 * @param aKey The key's Windows code.
 * @param aScanCode The key's place on the keyboard.
 * @param aType Pressed, released, or a typed character.
 * @param aE0 True for the keys Windows marks as extended with E0, such as the right Ctrl.
 * @param aE1 True for the keys Windows marks as extended with E1, such as Pause.
 */
void ProcessKeyboard(uint16_t aKey, uint16_t aScanCode, cef_key_event_type_t aType, bool aE0, bool aE1)
{
    if (aType != KEYEVENT_CHAR)
    {
        if (!aKey || aKey == 255)
        {
            return;
        }

        if (aKey == VK_SHIFT)
        {
            aKey = static_cast<uint16_t>(MapVirtualKey(aScanCode, MAPVK_VSC_TO_VK_EX));
        }
        else if (aKey == VK_NUMLOCK)
        {
            aScanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC) | 0x100);
        }

        if (aE1)
        {
            if (aKey == VK_PAUSE)
            {
                aScanCode = 0x45;
            }
            else
            {
                aScanCode = static_cast<uint16_t>(MapVirtualKey(aKey, MAPVK_VK_TO_VSC));
            }
        }

        if (aE0)
        {
            switch (aKey)
            {
            case VK_CONTROL: aKey = VK_RCONTROL; break;
            case VK_MENU: aKey = VK_RMENU; break;
            case VK_RETURN: aKey = VK_SEPARATOR; break;
            }
        }
        else
        {
            switch (aKey)
            {
            case VK_CONTROL: aKey = VK_LCONTROL; break;
            case VK_MENU: aKey = VK_LMENU; break;
            case VK_INSERT: aKey = VK_NUMPAD0; break;
            case VK_DELETE: aKey = VK_DECIMAL; break;
            case VK_HOME: aKey = VK_NUMPAD7; break;
            case VK_END: aKey = VK_NUMPAD1; break;
            case VK_PRIOR: aKey = VK_NUMPAD9; break;
            case VK_NEXT: aKey = VK_NUMPAD3; break;
            case VK_LEFT: aKey = VK_NUMPAD4; break;
            case VK_RIGHT: aKey = VK_NUMPAD6; break;
            case VK_UP: aKey = VK_NUMPAD8; break;
            case VK_DOWN: aKey = VK_NUMPAD2; break;
            case VK_CLEAR: aKey = VK_NUMPAD5; break;
            }
        }
    }

    OverlayService* pOverlay = s_pOverlay;
    OverlayApp* pApp = ReadyOverlayApp(pOverlay);
    if (!pApp)
        return;
    auto& overlay = *pOverlay;
    const auto active = overlay.GetActive();

    spdlog::debug("ProcessKey, type: {}, key: {}, active: {}", aType, aKey, active);

    if (aType != KEYEVENT_CHAR && (IsToggleKey(aKey) || (IsDisableKey(aKey) && active)))
    {
        if (overlay.GetInGame() && aType == KEYEVENT_KEYUP)
        {
            World::Get().ctx().at<MenuService>().Toggle(MenuSurface::kScreen);
        }
    }
    else if (active)
    {
        pApp->InjectKey(aType, GetCefModifiers(aKey), aKey, aScanCode);
    }
}

/**
 * Move the page's mouse pointer while the menu is open on the flat screen.
 *
 * @param aX Across the game window, in pixels.
 * @param aY Down the game window, in pixels.
 */
void ProcessMouseMove(uint16_t aX, uint16_t aY)
{
    OverlayService* pOverlay = s_pOverlay;
    OverlayApp* pApp = ReadyOverlayApp(pOverlay);
    if (pApp && pOverlay->GetActive())
        pApp->InjectMouseMove(aX, aY, GetCefModifiers(0));
}

/**
 * Press or let go of a mouse button on the page while the menu is open on the flat screen.
 *
 * @param aX Across the game window, in pixels.
 * @param aY Down the game window, in pixels.
 * @param aButton Which button.
 * @param aDown True when it is pressed, false when it is let go.
 */
void ProcessMouseButton(uint16_t aX, uint16_t aY, cef_mouse_button_type_t aButton, bool aDown)
{
    OverlayService* pOverlay = s_pOverlay;
    OverlayApp* pApp = ReadyOverlayApp(pOverlay);
    if (pApp && pOverlay->GetActive())
        pApp->InjectMouseButton(aX, aY, aButton, !aDown, GetCefModifiers(0));
}

/**
 * Scroll the page with the mouse wheel while the menu is open on the flat screen.
 *
 * @param aX Across the game window, in pixels.
 * @param aY Down the game window, in pixels.
 * @param aZ How far, in wheel units: 120 is one notch, positive scrolls up.
 */
void ProcessMouseWheel(uint16_t aX, uint16_t aY, int16_t aZ)
{
    OverlayService* pOverlay = s_pOverlay;
    OverlayApp* pApp = ReadyOverlayApp(pOverlay);
    if (pApp && pOverlay->GetActive())
        pApp->InjectMouseWheel(aX, aY, aZ, GetCefModifiers(0));
}

UINT GetRealACP()
{
    // Get the keyboard layout for the current thread.
    HKL keybdLayout = GetKeyboardLayout(0);

    // Extract the language ID from it, contained in its low-order word.
    int langID = LOWORD(keybdLayout);

    // Call the GetLocaleInfo function to retrieve the default ANSI code page
    // associated with that language ID.
    UINT acp = CP_ACP;
    GetLocaleInfo(MAKELCID(langID, SORT_DEFAULT),
        LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
        (LPTSTR) &acp,
        sizeof(acp) / sizeof(TCHAR));
    return acp;
}

/**
 * Pass the game window's messages to the page, Discord and the debug windows while the overlay is up.
 *
 * @param hwnd The game window.
 * @param uMsg The message.
 * @param wParam The message's first value.
 * @param lParam The message's second value.
 * @return Always 0, so the game's own window procedure still handles every message; anything else would keep it from the game.
 */
LRESULT CALLBACK InputService::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // The window procedure (BSGraphicsRenderer.cpp) stays hooked longer than this service lives.
    if (!ReadyOverlayApp(s_pOverlay))
        return 0;

    auto& discord = World::Get().ctx().at<DiscordService>();
    discord.WndProcHandler(hwnd, uMsg, wParam, lParam);

    const bool active = s_pOverlay->GetActive();
    if (active)
    {
        auto& imgui = World::Get().ctx().at<ImguiService>();
        imgui.WndProcHandler(hwnd, uMsg, wParam, lParam);
    }

    POINT position;

    GetCursorPos(&position);
    ScreenToClient(GetActiveWindow(), &position);

    ProcessMouseMove(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y));

    if (uMsg == WM_INPUT)
    {
        RAWINPUT input;
        UINT size = sizeof(RAWINPUT);

        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

        if (active)
        {
            auto& imgui = World::Get().ctx().at<ImguiService>();
            imgui.RawInputHandler(input);
        }

        if (input.header.dwType == RIM_TYPEKEYBOARD)
        {
            const auto keyboard = input.data.keyboard;

            ProcessKeyboard(keyboard.VKey, keyboard.MakeCode, keyboard.Flags & RI_KEY_BREAK ? KEYEVENT_KEYUP : KEYEVENT_KEYDOWN, keyboard.Flags & RI_KEY_E0, keyboard.Flags & RI_KEY_E1);
        }
        else if (input.header.dwType == RIM_TYPEMOUSE)
        {
            const auto mouse = input.data.mouse;

            if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
            {
                ProcessMouseWheel(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), (int16_t)mouse.usButtonData);
            }

            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_LEFT, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_LEFT, false);
            }

            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_RIGHT, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_RIGHT, false);
            }

            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_MIDDLE, true);
            }

            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            {
                ProcessMouseButton(static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), MBT_MIDDLE, false);
            }
        }
    }
    else if (uMsg == WM_CHAR)
    {
        uint16_t scancode = (lParam >> 16) & 0xFF;
        uint16_t virtualKey = static_cast<uint16_t>(wParam);
        if (!IsWindowUnicode(hwnd))
        {
            wchar_t wch;
            ::MultiByteToWideChar(s_currentACP, MB_PRECOMPOSED, reinterpret_cast<char*>(&virtualKey), 2, &wch, sizeof(wchar_t));
            virtualKey = wch;
        }
        ProcessKeyboard(virtualKey, scancode, KEYEVENT_CHAR, false, false);
    }
    // If the player tabs out/in with UI visible, this WndProc doesn't run during mouse or keyboard events.
    // When player tabs in, force the UI state
    else if (uMsg == WM_SETFOCUS && s_pOverlay->GetActive())
    {
        World::Get().ctx().at<MenuService>().Refresh();
    }
    else if (uMsg == WM_INPUTLANGCHANGE)
    {
        s_currentACP = GetRealACP();
        spdlog::info("Input language changed, current ACP: {}", s_currentACP);
    }

    return 0;
}

InputService::InputService(OverlayService& aOverlay) noexcept
{
    s_pOverlay = &aOverlay;
    s_currentACP = GetRealACP();
}

InputService::~InputService() noexcept
{
    s_pOverlay = nullptr;
}
