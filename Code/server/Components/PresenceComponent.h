#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/ActorPresence.h>

//! Which games have this character loaded, from each game's latest report (RequestActorPresence).
struct PresenceComponent
{
    struct Seen
    {
        uint32_t PlayerId{};
        ActorPresenceKind Kind{ActorPresenceKind::Own};
        bool Dead{};
        std::chrono::steady_clock::time_point At{};
    };

    //! A game reports every 2 s, so a report older than this means it no longer has the character.
    static constexpr std::chrono::seconds kFresh{5};

    TiltedPhoques::Vector<Seen> SeenBy{};
};
