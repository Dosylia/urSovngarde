#pragma once

struct OverlayService;

/**
 * @brief Handles input handling for the UI.
 */
struct InputService
{
    InputService(OverlayService& aOverlay) noexcept;
    ~InputService() noexcept;

    /**
     * Pass the game window's messages to the page, Discord and the debug windows while the overlay is up.
     *
     * @param hwnd The game window.
     * @param uMsg The message.
     * @param wParam The message's first value.
     * @param lParam The message's second value.
     * @return Always 0, so the game's own window procedure still handles every message; anything else would keep it from the game.
     */
    static LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    TP_NOCOPYMOVE(InputService);
};
