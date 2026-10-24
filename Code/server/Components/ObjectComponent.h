#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/LockData.h>
#include <Game/Player.h>

struct ObjectComponent
{
    ObjectComponent(Player* apLastSender)
        : pLastSender(apLastSender)
    {
    }

    Player* pLastSender;
    LockData CurrentLockData{};
    //! A door's state after the last activation passed on while its cell was occupied; 0 when none came.
    uint8_t DoorOpenState{};
};
