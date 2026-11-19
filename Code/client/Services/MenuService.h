#pragma once

#include <atomic>

struct World;
struct UpdateEvent;

/**
 * Where the menu page is shown.
 */
enum class MenuSurface : uint8_t
{
    kNone,
    // SteamVR's dashboard tab.
    kDashboard,
    // A panel inside the game, in VR.
    kPanel,
    // Over the game, on a flat screen.
    kScreen,
};

/**
 * Opens and closes the menu page, wherever it is shown.
 * Every way of opening it goes through here: SteamVR's dashboard tab, the flat game's keys, the in-game panel, other plugins.
 * One surface at a time, and only while the player is in a game: leaving the game closes it.
 * The page is told where it is shown, so it can fit its controls to that surface.
 */
struct MenuService
{
#ifdef SKYRIMVR
    // The surface this game shows its own menu on.
    static constexpr MenuSurface kInGame = MenuSurface::kPanel;
#else
    static constexpr MenuSurface kInGame = MenuSurface::kScreen;
#endif

    /**
     * Start with the menu closed, and check every frame whether the player left the game.
     *
     * @param aWorld The client's world, which holds the overlay service.
     * @param aDispatcher The event dispatcher that sends each frame's update.
     */
    MenuService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~MenuService() noexcept = default;

    TP_NOCOPYMOVE(MenuService);

    /**
     * Open the menu on a surface, or move it there from another one.
     *
     * @param aSurface Where to show it.
     * @return False when it cannot open now: the player is not in a game, a loading screen shows, or nothing draws that surface yet.
     */
    bool Open(MenuSurface aSurface) noexcept;
    /**
     * Close the menu, wherever it is shown.
     */
    void Close() noexcept;
    /**
     * Close the menu only if it is shown on one surface, so a surface going away leaves the menu shown elsewhere.
     *
     * @param aSurface The surface going away.
     */
    void CloseOn(MenuSurface aSurface) noexcept;
    /**
     * Close the menu if it is shown on a surface, otherwise open it there.
     *
     * @param aSurface The surface the player's key or button belongs to.
     */
    void Toggle(MenuSurface aSurface) noexcept;
    /**
     * Put the menu's effects back as they should be, after the game window got its focus back.
     */
    void Refresh() noexcept;
    /**
     * Tell whether a flat test rig game's window is in the background.
     * Such a game reads the rig's keys there, but never the real keyboard and mouse.
     * Called from the input poll when its window goes to the background or comes back to the front.
     *
     * @param aInBackground True while the rig game's window is in the background.
     */
    void SetRigGameInBackground(bool aInBackground) noexcept;
    /**
     * Tell whether something draws a surface now; a surface nothing draws does not open, so no menu is ever open unseen.
     * Only the panel needs this: the dashboard and the screen are drawn whenever their game runs.
     * A panel that stops being drawn closes the menu on it.
     *
     * @param aSurface The surface.
     * @param aShowable True once something draws it.
     */
    void SetShowable(MenuSurface aSurface, bool aShowable) noexcept;

    /**
     * Check if the menu is open, on any surface. Any thread.
     *
     * @return True while it is shown somewhere.
     */
    [[nodiscard]] bool IsOpen() const noexcept { return GetSurface() != MenuSurface::kNone; }
    /**
     * Find where the menu is shown. Any thread.
     *
     * @return The surface, or none while it is closed.
     */
    [[nodiscard]] MenuSurface GetSurface() const noexcept { return m_surface.load(); }

private:
    /**
     * Close the menu when the player leaves the game, as a load or the main menu does.
     *
     * @param acEvent This frame's update; only its arrival matters.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    /**
     * Check if the menu can open on a surface now.
     *
     * @param aSurface Where it would open.
     * @return False while the player is not in a game, a loading screen shows, or nothing draws that surface yet.
     */
    bool CanOpenNow(MenuSurface aSurface) const noexcept;
    /**
     * Make everything follow the menu's state: the page, the game's input, the flat cursor, and the real keyboard and mouse.
     */
    void ApplyMenuState() noexcept;
    /**
     * On a flat screen, show the page's own cursor while the menu is open; Windows' cursor stays hidden either way.
     *
     * @param aShow True while the menu is open.
     */
    void ShowFlatCursor(bool aShow) noexcept;
    /**
     * On a flat screen, keep the real keyboard and mouse from the game while the menu has them, or while a rig game is in the background.
     */
    void ApplyRealInput() noexcept;

    World& m_world;
    std::atomic<MenuSurface> m_surface{MenuSurface::kNone};
    bool m_panelShowable{false};
    bool m_rigGameInBackground{false};

    entt::scoped_connection m_updateConnection;
};
