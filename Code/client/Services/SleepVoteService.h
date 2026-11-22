#pragma once

#include <Messages/RequestTimeSkip.h>

#include <string>

struct TransportService;
class CalendarService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyTimeSkip;
struct ServerTimeSettings;

/**
 * This game's side of the shared time skip vote: it sends the player's confirmation, withdrawal and wake-up, and shows where the vote stands.
 * The Sleep/Wait menu's OK button (SleepWaitOk) holds the press; once every player agrees, the main loop presses the game's own OK.
 */
struct SleepVoteService
{
    /**
     * Listen for the frame's update, the server's vote notices and time resyncs, and the end of the connection.
     *
     * @param aDispatcher Where the client's events and messages come from.
     * @param aTransport The connection to the server.
     * @param aCalendar The game's clock, which a shared skip leaves to the game while it rests.
     */
    SleepVoteService(entt::dispatcher& aDispatcher, TransportService& aTransport, CalendarService& aCalendar) noexcept;

    TP_NOCOPYMOVE(SleepVoteService);

private:
    /**
     * Take the vote back when its menu closed or combat began, send a pressed OK, report the end of the game's own rest, and follow the wait after a refusal.
     *
     * @param acEvent The frame's update.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    /**
     * Drop this game's vote with the connection.
     *
     * @param acEvent The end of the connection.
     */
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    /**
     * Follow the vote: show each change, start the agreed skip, release a vote that ran out, or rest alone where time is not shared.
     *
     * @param acMessage Where the vote stands.
     */
    void OnNotifyTimeSkip(const NotifyTimeSkip& acMessage) noexcept;

    /**
     * Note the first time resync after an agreed skip, for the log.
     *
     * @param acMessage The server's time.
     */
    void OnTimeResync(const ServerTimeSettings& acMessage) noexcept;

    /**
     * Send a pressed OK as this player's confirmation.
     */
    void SendPress() noexcept;

    /**
     * Take the vote back when its menu is gone or the player is in combat, and tell the server when it holds the confirmation.
     */
    void WithdrawWhenMenuGoneOrFighting() noexcept;

    /**
     * Note that the game's own rest is over, once the game no longer sleeps or waits.
     */
    void NoteRestOver() noexcept;

    /**
     * Give the clock back to the server once the skip is over here, rested or not, and tell the server.
     */
    void EndSkipOnceRested() noexcept;

    /**
     * Say in the menu how long is left when the player presses OK or opens a menu during the wait after a refusal, and take the line away once the wait is over.
     */
    void FollowWaitAfterRefusal() noexcept;

    /**
     * Show the wait after a refusal in the menu.
     *
     * @param aSeconds The seconds left.
     */
    void ShowWaitLine(uint32_t aSeconds) noexcept;

    /**
     * Put the line that matters most in the menu: the wait after a refusal, else where the vote stands, else the menu's own question.
     */
    void RefreshMenuLine() noexcept;

    /**
     * Send one step of this player's vote.
     *
     * @param aStep What the player did.
     * @param aHours The length, in game hours; read with a confirmation only.
     */
    void SendStep(RequestTimeSkip::Step aStep, float aHours) noexcept;

    /**
     * Show a change to the vote: whom it waits for to a player in it, who changed what to the others.
     *
     * @param acMessage Where the vote stands.
     */
    void ShowVoteChange(const NotifyTimeSkip& acMessage) noexcept;

    /**
     * Start the agreed skip: the main loop presses the game's own OK for the agreed length.
     *
     * @param acMessage The agreement, with the length.
     */
    void StartAgreedSkip(const NotifyTimeSkip& acMessage) noexcept;

    /**
     * Release a vote that ran out, and say who did not confirm.
     *
     * @param acMessage The end of the vote, with the missing names.
     */
    void ReleaseRunOutVote(const NotifyTimeSkip& acMessage) noexcept;

    /**
     * Rest this player's own length, as the game does offline, where the server does not share the time.
     */
    void RestAlone() noexcept;

    /**
     * Release this player's press when the server refused it as too soon after a withdrawal or a vote that ran out.
     * Presses are then dropped for the wait, and the menu and the HUD say how long it is.
     *
     * @param acMessage The refusal, with the wait.
     */
    void RefuseTooSoon(const NotifyTimeSkip& acMessage) noexcept;

    TransportService& m_transport;
    CalendarService& m_calendar;
    bool m_awaitingResync = false; // an agreed skip's time resync is still to come
    std::string m_voteLine;        // where the vote stands, for the menu; empty when no vote is open
    std::string m_waitLine;        // the wait after a refusal, for the menu; empty when the player does not wait
    const void* m_waitMenu = nullptr; // the menu the wait was last written for; only compared, never read

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_noticeConnection;
    entt::scoped_connection m_resyncConnection;
};
