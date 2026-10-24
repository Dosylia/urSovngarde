#pragma once

#include <string>

//! The line under "YOU DIED" that says how the player died, as Minecraft's death messages do.
//! Plain values in and out; the game facts behind them are gathered by DeathCause.
namespace DeathSentence
{
enum class How
{
    kMelee, //!< A weapon or a creature's own attack, up close.
    kShot,  //!< An arrow or a bolt.
    kMagic, //!< A spell, a scroll or a shout.
    kBash,  //!< A shield or weapon bash.
};

struct Cause
{
    //! Who landed the last blow before the player went down; empty when nobody did.
    std::string Killer;
    //! What it was done with ("Iron War Axe", "Firebolt"); empty when nothing worth naming.
    std::string With;
    How Kind = How::kMelee;
    //! The player dropped from high up just before going down.
    bool Fell = false;
};

inline std::string Describe(const std::string& acVictim, const Cause& acCause)
{
    const std::string& cKiller = acCause.Killer;
    if (acCause.Fell)
        return cKiller.empty() ? acVictim + " fell from a high place" : acVictim + " was doomed to fall by " + cKiller;
    if (cKiller.empty())
        return acVictim + " died";

    switch (acCause.Kind)
    {
    case How::kShot: return acVictim + " was shot by " + cKiller;
    case How::kMagic: return acVictim + " was killed by " + cKiller + " using " + (acCause.With.empty() ? std::string("magic") : acCause.With);
    case How::kBash: return acVictim + " was bashed by " + cKiller;
    case How::kMelee: break;
    }
    return acCause.With.empty() ? acVictim + " was slain by " + cKiller : acVictim + " was slain by " + cKiller + " using " + acCause.With;
}

//! The card writes it in capitals; only plain letters change, so a name in another script stays as it is.
inline std::string Capitals(std::string aText)
{
    for (char& c : aText)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return aText;
}
} // namespace DeathSentence
