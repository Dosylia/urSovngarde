
#include <windows.h>
#include <OverlayProc.hpp>
#include "ProcessHandler.h"

#include <cstdlib>
#include <cstring>

namespace
{
// The game gives every helper it starts its own process id ("--pid=N"); 0 when the switch is missing.
DWORD GetGameProcessId(const char* acpCommandLine) noexcept
{
    constexpr char cSwitch[] = "--pid=";
    for (const char* pFound = std::strstr(acpCommandLine, cSwitch); pFound; pFound = std::strstr(pFound + 1, cSwitch))
    {
        if (pFound == acpCommandLine || pFound[-1] == ' ')
            return std::strtoul(pFound + sizeof(cSwitch) - 1, nullptr, 10);
    }
    return 0;
}

DWORD WINAPI WaitForGameEnd(LPVOID apGameProcess)
{
    WaitForSingleObject(static_cast<HANDLE>(apGameProcess), INFINITE);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

// A helper ends when the game ends, however it ends.
// After a crash, a helper left running keeps Mod Organizer's virtual file system loaded, and the next Mod Organizer start waits for it.
void EndWithGame(const char* acpCommandLine) noexcept
{
    const DWORD cGameId = GetGameProcessId(acpCommandLine);
    if (cGameId == 0)
        return;

    const HANDLE cGame = OpenProcess(SYNCHRONIZE, FALSE, cGameId);
    if (!cGame)
    {
        // No such process: the game is gone already.
        if (GetLastError() == ERROR_INVALID_PARAMETER)
            TerminateProcess(GetCurrentProcess(), 0);
        return;
    }

    if (const HANDLE cThread = CreateThread(nullptr, 0, &WaitForGameEnd, cGame, 0, nullptr))
        CloseHandle(cThread);
}
} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    EndWithGame(lpCmdLine);

    return TiltedPhoques::UIMain(lpCmdLine, hInstance, []() { return new ProcessHandler; });
}
