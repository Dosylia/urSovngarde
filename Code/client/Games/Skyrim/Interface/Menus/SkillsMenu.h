#pragma once

#include <Games/Events.h>
#include <Interface/IMenu.h>

struct MenuOpenCloseEvent;

struct MenuEventHandler
{
    char pad0[0x10];
};

struct StatsMenu final : IMenu, MenuEventHandler
{
    // TBD
};

#ifdef SKYRIMVR
// True while the server unpauses the skills menu and its checks are skipped: the menu may then be shown unpaused.
bool SkillsMenuCanRunUnpaused() noexcept;
// Called from the start of Main::Update (SkyrimVM64.cpp).
void UpdateSkillsMenuChecks();
void RunDeferredSkillsTreeUpdate();
void ReleaseClosedSkillsTrees();
#endif
