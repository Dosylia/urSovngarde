#include <TiltedOnlinePCH.h>

#include <Services/MenuService.h>
#include <Services/OverlayService.h>

#include <Events/UpdateEvent.h>
#include <Games/Skyrim/BSInput/GameInputHold.h>

#include <World.h>
#include <Games/Skyrim/Interface/UI.h>

#ifndef SKYRIMVR
#include <DInputHook.hpp>
#include <OverlayClient.hpp>
#include <OverlayRenderHandler.hpp>
#endif

namespace
{
/**
 * Name a surface for the log and for the page.
 *
 * @param aSurface The surface.
 * @return "dashboard", "panel", "screen" or "none".
 */
const char* SurfaceName(const MenuSurface aSurface) noexcept
{
    switch (aSurface)
    {
    case MenuSurface::kDashboard: return "dashboard";
    case MenuSurface::kPanel: return "panel";
    case MenuSurface::kScreen: return "screen";
    default: return "none";
    }
}
} // namespace

/**
 * Start with the menu closed, and check every frame whether the player left the game.
 *
 * @param aWorld The client's world, which holds the overlay service.
 * @param aDispatcher The event dispatcher that sends each frame's update.
 */
MenuService::MenuService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&MenuService::OnUpdate>(this);
}

/**
 * Open the menu on a surface, or move it there from another one.
 *
 * @param aSurface Where to show it.
 * @return False when it cannot open now: the player is not in a game, a loading screen shows, or nothing draws that surface yet.
 */
bool MenuService::Open(const MenuSurface aSurface) noexcept
{
    if (!CanOpenNow(aSurface))
        return false;

    const MenuSurface cWas = m_surface.exchange(aSurface);
    if (cWas == aSurface)
        return true;

    if (cWas == MenuSurface::kNone)
        spdlog::info("MenuService: opened on the {}", SurfaceName(aSurface));
    else
        spdlog::info("MenuService: moved from the {} to the {}", SurfaceName(cWas), SurfaceName(aSurface));
    ApplyMenuState();
    return true;
}

/**
 * Close the menu, wherever it is shown.
 */
void MenuService::Close() noexcept
{
    const MenuSurface cWas = m_surface.exchange(MenuSurface::kNone);
    if (cWas == MenuSurface::kNone)
        return;

    spdlog::info("MenuService: closed on the {}", SurfaceName(cWas));
    ApplyMenuState();
}

/**
 * Close the menu only if it is shown on one surface, so a surface going away leaves the menu shown elsewhere.
 *
 * @param aSurface The surface going away.
 */
void MenuService::CloseOn(const MenuSurface aSurface) noexcept
{
    if (GetSurface() == aSurface)
        Close();
}

/**
 * Close the menu if it is shown on a surface, otherwise open it there.
 *
 * @param aSurface The surface the player's key or button belongs to.
 */
void MenuService::Toggle(const MenuSurface aSurface) noexcept
{
    if (GetSurface() == aSurface)
        Close();
    else
        Open(aSurface);
}

/**
 * Put the menu's effects back as they should be, after the game window got its focus back.
 */
void MenuService::Refresh() noexcept
{
    ApplyMenuState();
}

/**
 * Tell whether a flat test rig game's window is in the background.
 * Such a game reads the rig's keys there, but never the real keyboard and mouse.
 * Called from the input poll when its window goes to the background or comes back to the front.
 *
 * @param aInBackground True while the rig game's window is in the background.
 */
void MenuService::SetRigGameInBackground(const bool aInBackground) noexcept
{
    if (m_rigGameInBackground == aInBackground)
        return;
    m_rigGameInBackground = aInBackground;
    spdlog::info("MenuService: the rig game is in the {}; the real keyboard and mouse are {}", aInBackground ? "background" : "front", aInBackground ? "not read" : "read");
    ApplyRealInput();
}

/**
 * Tell whether something draws a surface now; a surface nothing draws does not open, so no menu is ever open unseen.
 * Only the panel needs this: the dashboard and the screen are drawn whenever their game runs.
 * A panel that stops being drawn closes the menu on it.
 *
 * @param aSurface The surface.
 * @param aShowable True once something draws it.
 */
void MenuService::SetShowable(const MenuSurface aSurface, const bool aShowable) noexcept
{
    if (aSurface != MenuSurface::kPanel || m_panelShowable == aShowable)
        return;
    m_panelShowable = aShowable;
    if (!aShowable)
        CloseOn(MenuSurface::kPanel);
}

/**
 * Close the menu when the player leaves the game, as a load or the main menu does.
 *
 * @param acEvent This frame's update; only its arrival matters.
 */
void MenuService::OnUpdate(const UpdateEvent&) noexcept
{
    // A load or the main menu takes the player out of the game, and the overlay drops the page's active state with them.
    if (IsOpen() && !m_world.GetOverlayService().GetActive())
    {
        spdlog::info("MenuService: closed on the {}, the player left the game", SurfaceName(GetSurface()));
        m_surface = MenuSurface::kNone;
        ApplyMenuState();
    }
}

/**
 * Check if the menu can open on a surface now.
 *
 * @param aSurface Where it would open.
 * @return False while the player is not in a game, a loading screen shows, or nothing draws that surface yet.
 */
bool MenuService::CanOpenNow(const MenuSurface aSurface) const noexcept
{
    if (aSurface == MenuSurface::kNone || !m_world.GetOverlayService().GetInGame())
        return false;

    // The player keeps their 3D through a door's load, but a loading screen is no place for the menu.
    static const BSFixedString s_loading("Loading Menu");
    if (const UI* pUi = UI::Get(); pUi && pUi->GetMenuOpen(s_loading))
        return false;

    if (aSurface == MenuSurface::kPanel && !m_panelShowable)
    {
        spdlog::info("MenuService: not opened on the panel, which nothing draws yet");
        return false;
    }
    return true;
}

/**
 * Make everything follow the menu's state: the page, the game's input, the flat cursor, and the real keyboard and mouse.
 */
void MenuService::ApplyMenuState() noexcept
{
    auto& overlay = m_world.GetOverlayService();
    const bool cOpen = IsOpen();
    if (cOpen)
        overlay.SetSurface(SurfaceName(GetSurface()));
    overlay.SetActive(cOpen);

    // No pause while it is open: the game runs on, without the player's input.
    // SteamVR's dashboard pauses the game and keeps the controllers itself, so the hold is for the menu inside the game.
    GameInputHold::SetHeld(cOpen && GetSurface() != MenuSurface::kDashboard);

    ShowFlatCursor(cOpen);
    ApplyRealInput();
}

/**
 * On a flat screen, show the page's own cursor while the menu is open; Windows' cursor stays hidden either way.
 *
 * @param aShow True while the menu is open.
 */
void MenuService::ShowFlatCursor(const bool aShow) noexcept
{
#ifndef SKYRIMVR
    if (OverlayApp* pApp = m_world.GetOverlayService().GetOverlayApp(); pApp && pApp->GetClient())
    {
        if (auto pRenderer = pApp->GetClient()->GetOverlayRenderHandler())
            pRenderer->SetCursorVisible(aShow);
    }
    // Windows counts how often its cursor was shown: hidden only once that count is below zero.
    while (ShowCursor(FALSE) >= 0)
        ;
#else
    (void)aShow;
#endif
}

/**
 * On a flat screen, keep the real keyboard and mouse from the game while the menu has them, or while a rig game is in the background.
 */
void MenuService::ApplyRealInput() noexcept
{
#ifndef SKYRIMVR
    // The game's reads of the real keyboard and mouse come back empty: the page has them while it is open.
    // A rig game in the background gets none either, so the person using the PC never plays it by typing elsewhere.
    TiltedPhoques::DInputHook::Get().SetEnabled(IsOpen() || m_rigGameInBackground);
#endif
}
