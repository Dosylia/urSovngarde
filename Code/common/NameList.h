#pragma once

#include <TiltedCore/Stl.hpp>

/** Player names written out for people: logs and HUD lines. */
namespace NameList
{
/**
 * Join names into one line, separated by commas.
 *
 * @param acNames The names.
 * @param acpNone What to write when there are none.
 * @return The line.
 */
inline TiltedPhoques::String Join(const TiltedPhoques::Vector<TiltedPhoques::String>& acNames, const char* acpNone)
{
    if (acNames.empty())
        return acpNone;

    TiltedPhoques::String line;
    for (const TiltedPhoques::String& cName : acNames)
    {
        if (!line.empty())
            line += ", ";
        line += cName;
    }
    return line;
}
} // namespace NameList
