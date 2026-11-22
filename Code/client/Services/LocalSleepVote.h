#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

/**
 * This game's part in the shared time skip vote, from the OK press in the Sleep/Wait menu to the end of the game's own sleep or wait.
 * The OK hook, the client's update and the main loop each use it, so every call takes its lock.
 * Plain values in and out, so the unit tests check it without the game (SleepVoteService and SleepWaitOk apply it).
 */
class LocalSleepVote
{
public:
    /** Where this game's vote stands. */
    enum class Phase
    {
        kIdle,      // no vote of this player's
        kPressed,   // OK pressed; the confirmation is still to be sent
        kConfirmed, // the confirmation is sent; not every player has confirmed yet
        kAgreed,    // the length is settled; the main loop is to press the game's own OK
        kResting,   // the game's own OK ran; the game sleeps or waits until it says it is done
    };

    /** What came of an OK press. */
    enum class PressResult
    {
        kHeld,        // held, to be sent as this player's confirmation
        kAlreadyHeld, // the length this game pressed or sent already; nothing new to send
        kWaiting,     // the server asked this player to wait before confirming again; the press is dropped
        kNotTaken,    // a length no menu offers, or the skip already under way
    };

    /**
     * Tell where this game's vote stands.
     *
     * @return The current phase.
     */
    Phase GetPhase();

    /**
     * Tell whether this game's vote waits on the other players: pressed or confirmed.
     *
     * @return True in those two phases.
     */
    bool IsVoting();

    /**
     * Hold the menu's OK press for the vote, replacing an earlier press of another length.
     *
     * @param apMenu The Sleep/Wait menu the press came from; only compared, never read.
     * @param aHours The length the player picked, in game hours.
     * @param aNowMs This game's steady time now, in milliseconds (SteadyNowMs).
     * @return kHeld when the press is to be sent; otherwise why not, and nothing changes.
     */
    PressResult Press(const void* apMenu, float aHours, uint64_t aNowMs);

    /**
     * Drop every OK press until a time, as the server asked after refusing one.
     *
     * @param aUntilMs This game's steady time the wait ends, in milliseconds.
     */
    void WaitBeforePressing(uint64_t aUntilMs);

    /**
     * Tell how long this game still drops OK presses.
     *
     * @param aNowMs This game's steady time now, in milliseconds.
     * @return The time left, in milliseconds; 0 when presses count again.
     */
    uint64_t GetWaitLeftMs(uint64_t aNowMs);

    /**
     * Take the note that a press was dropped during the wait, so the menu can say how long is left.
     *
     * @return True when a press was dropped since the last call.
     */
    bool TakeDroppedPress();

    /**
     * Read this game's steady time, which never goes back, for the wait.
     *
     * @return The time, in milliseconds.
     */
    static uint64_t SteadyNowMs() noexcept;

    /**
     * Take a press still to be sent, which makes it confirmed.
     *
     * @return The pressed length, in game hours; nothing when no press waits.
     */
    std::optional<float> TakePress();

    /**
     * Tell whether the menu the vote was pressed in is no longer open: closed, or closed and opened again.
     *
     * @param apOpenMenu The Sleep/Wait menu open now; null when none is.
     * @return True while pressed or confirmed and the open menu is not the one pressed in.
     */
    bool IsMenuGone(const void* apOpenMenu);

    /**
     * Take the vote back: its menu closed, combat began, the vote ran out, or the server refused the press; a skip already under way is kept.
     *
     * @return True when a confirmation of this vote was sent, so the server may still hold it.
     */
    bool Withdraw();

    /**
     * Settle the length every player agreed on, so the main loop presses the game's own OK.
     *
     * @param aHours The agreed length, in game hours.
     * @return False when this game holds no pressed or confirmed vote; nothing changes then.
     */
    bool Agree(float aHours);

    /**
     * Settle on this player's own pressed length, when the server does not share the time.
     *
     * @return False when this game holds no pressed or confirmed vote; nothing changes then.
     */
    bool AgreeAlone();

    /**
     * Take the agreed length for the game's own OK, which makes the game resting.
     * When the menu pressed in is gone, the vote is dropped instead.
     *
     * @param apOpenMenu The Sleep/Wait menu open now; null when none is.
     * @return The agreed length, in game hours; nothing when none is agreed or the menu is gone.
     */
    std::optional<float> TakeAgreed(const void* apOpenMenu);

    /**
     * Note that the game no longer sleeps or waits.
     *
     * @return True when the game was resting from the vote, so the server must be told.
     */
    bool FinishResting();

    /**
     * Drop everything whatever the phase, the wait included, after the connection was lost.
     */
    void Reset();

private:
    /**
     * Tell whether a phase waits on the other players: pressed or confirmed.
     *
     * @param aPhase The phase.
     * @return True for those two phases.
     */
    static bool IsVotingPhase(Phase aPhase) noexcept;

    std::mutex m_lock;
    Phase m_phase = Phase::kIdle;
    const void* m_pMenu = nullptr; // the menu the press came from
    float m_hours = 0.f;           // the pressed length, then the agreed one, in game hours
    bool m_confirmationSent = false; // a confirmation of this vote went to the server, even if a newer press waits
    uint64_t m_waitUntilMs = 0;    // steady time until which presses are dropped
    bool m_pressDropped = false;   // a press was dropped during the wait and the menu has not said so yet
};
