#pragma once

#include <Games/Events.h>
#include <Interface/IMenu.h>

class MenuOpenCloseEvent;

struct IMapCameraCallbacks
{
    // add
    virtual void Unk_00(void); // 00
    virtual void Unk_01(void); // 01

    virtual ~IMapCameraCallbacks(); // 02
};
static_assert(sizeof(IMapCameraCallbacks) == 0x8);

struct MapMenu final : public IMenu, public BSTEventSink<MenuOpenCloseEvent>, public IMapCameraCallbacks
{
    // TBD
};

#ifdef SKYRIMVR
/**
 * @brief Shuts the world map, if it is open. Safe to call when it is not, and from any thread.
 *
 * The map does not pause the game online in VR, so something has to take it away from a player who needs to be
 * looking at the world instead. See MapMenu.cpp for how and when it is carried out, and PlayerService for what asks.
 */
void CloseMapMenu() noexcept;

// True once the map's hooks are in place: only then may it be shown unpaused.
bool MapMenuCanRunUnpaused() noexcept;

// Whether the map is shown: between the game's show and hide of it, whatever thread handled them.
bool MapMenuShown() noexcept;
#endif
