#include <TiltedOnlinePCH.h>
#include <Interface/Menus/SleepWaitVoteLine.h>

#include <Interface/GFxMovieView.h>
#include <Interface/GFxValue.h>
#include <Interface/IMenu.h>
#include <Interface/Menus/SleepWaitOk.h>

#include <mutex>
#include <optional>

namespace
{
// The question's text field: the same instance name in the game's own menu and in replacers that keep it (Dear Diary VR).
constexpr const char* kQuestionPath = "SleepWaitMenu_mc.QuestionInstance";

// The line wanted, set from any thread and read by the main loop; nothing means the question.
std::mutex s_wantedLock;
std::optional<std::string> s_wanted;

// The main loop's own state.
std::string s_question;                  // the menu's own question, seen in the field before a line replaced it
std::string s_shown;                     // the line written last; empty when the question shows
const void* s_pMenuWithoutField = nullptr; // a menu found with no question field, not searched again

/**
 * Copy the line wanted now.
 *
 * @return The line; nothing when the question is wanted.
 */
std::optional<std::string> CopyWanted() noexcept
{
    std::scoped_lock lock(s_wantedLock);
    return s_wanted;
}

/**
 * Find the question's text field in a menu, remembering a menu that has none.
 *
 * @param acMenu The open Sleep/Wait menu.
 * @param aOut Where the field goes; it must be empty.
 * @return False when the menu's movie has no such text field.
 */
bool FindQuestion(const IMenu& acMenu, GFxValue& aOut) noexcept
{
    if (s_pMenuWithoutField == &acMenu)
        return false;

    const auto* pMovie = static_cast<const GFxMovieView*>(acMenu.uiMovie);
    if (pMovie && pMovie->GetVariable(aOut, kQuestionPath) && aOut.Holds(GFxValue::Type::kDisplayObject))
        return true;

    spdlog::warn("SleepVote: this Sleep/Wait menu has no text field {}; the vote shows on the HUD only", kQuestionPath);
    s_pMenuWithoutField = &acMenu;
    return false;
}

/**
 * Read the text a text field holds.
 *
 * @param acField The text field.
 * @return The text; empty when it holds none.
 */
std::string ReadText(const GFxValue& acField) noexcept
{
    GFxValue text;
    const char* pText = acField.GetMember("text", text) ? text.GetString() : nullptr;
    return pText ? pText : std::string();
}

/**
 * Write the line into the question's field, keeping the menu's question when the field still shows it, and log what the field then holds.
 *
 * @param aField The question's text field.
 * @param acCurrent The text the field holds now.
 * @param acLine The line.
 */
void WriteLine(GFxValue& aField, const std::string& acCurrent, const std::string& acLine) noexcept
{
    // Text that is not our own line is the menu's question: a new menu, or the game wrote it again.
    if (acCurrent != s_shown)
        s_question = acCurrent;

    aField.SetText(acLine.c_str());
    s_shown = acLine;
    spdlog::info("SleepVote: the menu shows '{}'", ReadText(aField));
}

/**
 * Put the menu's own question back in place of the line.
 *
 * @param aField The question's text field.
 */
void RestoreQuestion(GFxValue& aField) noexcept
{
    aField.SetText(s_question.c_str());
    s_shown.clear();
    spdlog::info("SleepVote: the menu's question is back, '{}'", ReadText(aField));
}
} // namespace

/**
 * Show a line in the open menu instead of its question. Any thread.
 *
 * @param acLine The line, short enough for one line of the question's field.
 */
void SleepWaitVoteLine::Show(const std::string& acLine) noexcept
{
    std::scoped_lock lock(s_wantedLock);
    s_wanted = acLine;
}

/**
 * Show the menu's own question again. Any thread.
 */
void SleepWaitVoteLine::Clear() noexcept
{
    std::scoped_lock lock(s_wantedLock);
    s_wanted.reset();
}

/**
 * Write the line into the open menu, or put the question back, once it changed.
 * Called from the main loop, on the main thread, where the game's own menu code runs.
 */
void SleepWaitVoteLine::Update() noexcept
{
    const auto cWanted = CopyWanted();
    // Nothing to write and nothing to take back: the menu's movie is left alone.
    if (!cWanted && s_shown.empty())
        return;

    const auto* pMenu = static_cast<const IMenu*>(SleepWaitOk::FindOpenMenu());
    GFxValue field;
    if (!pMenu || !FindQuestion(*pMenu, field))
        return;

    // Compared with what the field shows each frame, so a new menu at the same address, or the game writing the field again, is seen.
    const std::string cCurrent = ReadText(field);
    if (cWanted && cCurrent != *cWanted)
        WriteLine(field, cCurrent, *cWanted);
    else if (!cWanted && cCurrent == s_shown)
        RestoreQuestion(field);
    else if (!cWanted)
        s_shown.clear();
}
