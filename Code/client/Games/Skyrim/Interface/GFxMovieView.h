#pragma once

struct GFxValue;

/**
 * The game's Scaleform movie (Scaleform::GFx::MovieView) a menu draws: only its variable lookup is used.
 * A menu's movie is IMenu::uiMovie.
 */
struct GFxMovieView
{
    /**
     * Read a variable of the movie by its ActionScript path, as "SleepWaitMenu_mc.QuestionInstance".
     *
     * @param aOut Where the value goes; it must be empty.
     * @param acpPath The path, from the movie's root.
     * @return False when the movie has no such variable.
     */
    bool GetVariable(GFxValue& aOut, const char* acpPath) const noexcept;
};
