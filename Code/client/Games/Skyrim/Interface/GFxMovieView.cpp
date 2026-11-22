#include <TiltedOnlinePCH.h>
#include <Interface/GFxMovieView.h>

#include <Interface/GFxValue.h>

namespace
{
// GFxMovie::GetVariable's slot in the movie's vtable: the same in both games, as their Sleep/Wait menus call it (+0x88).
constexpr size_t kGetVariableSlot = 0x11;

using TGetVariable = bool(const GFxMovieView* apMovie, GFxValue* apOut, const char* acpPath);
} // namespace

/**
 * Read a variable of the movie by its ActionScript path, as "SleepWaitMenu_mc.QuestionInstance".
 *
 * @param aOut Where the value goes; it must be empty.
 * @param acpPath The path, from the movie's root.
 * @return False when the movie has no such variable.
 */
bool GFxMovieView::GetVariable(GFxValue& aOut, const char* acpPath) const noexcept
{
    // The movie's first field points at its vtable, an array of function pointers.
    using TVtable = TGetVariable* const*;
    const TVtable cVtable = *reinterpret_cast<const TVtable*>(this);
    return cVtable[kGetVariableSlot](this, &aOut, acpPath);
}
