#include <TiltedOnlinePCH.h>
#include <Interface/Menus/SleepWaitMenu.h>

#ifdef SKYRIMVR
#include <Interface/DeferredSleepWait.h>
#include <Interface/IMenu.h>
#include <Interface/UI.h>
#include <World.h>
#include <AI/Movement/PlayerControls.h>
#include <PlayerCharacter.h>
#include <Services/CalendarService.h>

namespace
{
// SkyrimVR 1.4.15. These are the sleep hooks from the VR menu branch, kept independent of Map/Skills.
constexpr uintptr_t kProcessMessage = 0x8EA840;
constexpr uintptr_t kMainThreadCheck = 0x8EA896;
constexpr uint8_t kMainThreadJump[]{0x0F, 0x85, 0xD4, 0x02, 0x00, 0x00};
constexpr uintptr_t kOkCallback = 0x8EACA0;
constexpr uintptr_t kSleepWaitStep = 0x6C2C00;
constexpr uintptr_t kPlayerSleepWaitCalls[]{0x6BF425, 0x6BFDF5};
constexpr uintptr_t kPlayerControlsCanProcess = 0x72DB00;
constexpr uintptr_t kMeleeSwingMenuCheck = 0x6B93D9;
constexpr uintptr_t kMainUpdate = 0x5BAB10;

DeferredSleepWait s_accept;
bool s_hooksReady = false;

// ProcessMessage's main-thread check: the unpaused menu needs it skipped, so its input is handled on the UI worker.
// It is skipped only while the server lets a sleep pass time; elsewhere the menu runs exactly as in the game.
// The jne's first two bytes are swapped for a short jump over it in one aligned write, so a thread running it sees either form whole.
constexpr uint16_t kCheckKept = 0x850F;    // 0F 85: the game's jne
constexpr uint16_t kCheckSkipped = 0x04EB; // EB 04: a jump over the jne's other four bytes
uint8_t* s_pMainThreadCheck = nullptr;
// Read by the menu queue on another thread.
std::atomic<bool> s_checkSkipped{false};

void SetMainThreadCheckSkipped(bool aSkipped) noexcept
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(s_pMainThreadCheck, sizeof(uint16_t), PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    InterlockedExchange16(reinterpret_cast<volatile SHORT*>(s_pMainThreadCheck), static_cast<SHORT>(aSkipped ? kCheckSkipped : kCheckKept));
    VirtualProtect(s_pMainThreadCheck, sizeof(uint16_t), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), s_pMainThreadCheck, sizeof(uint16_t));
    s_checkSkipped = aSkipped;
    spdlog::info("Sleep/Wait: {}", aSkipped ? "this server lets a sleep pass time, so the menu will not pause" : "the menu pauses as in the game");
}

using TOk = void(void*);
using TSleepStep = void(void*);
using TCanProcess = bool(void*);
using TMenuOpen = bool(const UI*, const BSFixedString&);
TOk* RealOk;
TSleepStep* RealSleepStep;
TCanProcess* RealCanProcess;
TMenuOpen* RealMeleeMenuCheck;
TP_THIS_FUNCTION(TProcessMessage, uint32_t, IMenu, UIMessage&);
TProcessMessage* RealProcessMessage;

IMenu* GetOpenSleepMenu()
{
    static const BSFixedString s_name("Sleep/Wait Menu");
    UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(s_name) ? pUI->FindMenuByName(s_name) : nullptr;
}

bool IsUnpausedSleepOpen()
{
    const IMenu* pMenu = GetOpenSleepMenu();
    return pMenu && !pMenu->PausesGame();
}

uint32_t HookProcessMessage(IMenu* apMenu, UIMessage& aMessage)
{
    if (aMessage.eType == UIMessage::kShow)
        s_accept.Open(apMenu);
    else if (aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide)
        s_accept.Close(apMenu);
    return TiltedPhoques::ThisCall(RealProcessMessage, apMenu, aMessage);
}

void HookOk(void* apArgs)
{
    // FxDelegateArgs: handler at +0x18, GFxValue array at +0x28; first numeric value at +0x10.
    auto* pArgs = static_cast<uint8_t*>(apArgs);
    auto* pMenu = *reinterpret_cast<IMenu**>(pArgs + 0x18);
    if (!pMenu || pMenu->PausesGame())
    {
        RealOk(apArgs);
        return;
    }

    const auto* pValues = *reinterpret_cast<const uint8_t**>(pArgs + 0x28);
    // An open unpaused menu retains its behavior after disconnect; a new offline confirmation is local.
    if (!World::Get().GetTransport().IsConnected())
        s_accept.Resolve(true);
    s_accept.Queue(pMenu, *reinterpret_cast<const double*>(pValues + 0x10));
}

void HookSleepStep(void* apPlayer)
{
    // ProcessMessage's timer owns the countdown. Leave its call at +0x8EA9E7 and jail fast-forward alone.
    // Without this guard both regular player updates consume an hour every frame while unpaused.
    if (!IsUnpausedSleepOpen())
        RealSleepStep(apPlayer);
}

bool HookCanProcess(void* apControls)
{
    return !CalendarService::AreSleepControlsLocked() && !IsUnpausedSleepOpen() && RealCanProcess(apControls);
}

bool HookMeleeMenuCheck(const UI* apUI, const BSFixedString& acName)
{
    return CalendarService::AreSleepControlsLocked() || RealMeleeMenuCheck(apUI, acName) || IsUnpausedSleepOpen();
}
}

bool SleepWaitCanRunUnpaused() noexcept
{
    return s_hooksReady && s_checkSkipped;
}

uint64_t SleepWaitMenuGeneration()
{
    return s_accept.GetGeneration();
}

void ResolveDeferredSleepWait(bool aAllowed)
{
    s_accept.Resolve(aAllowed);
}

void UpdateSleepWatchControls()
{
    // Changed only while no Sleep/Wait menu exists, not even one queued, so its code is not rewritten under a message.
    const bool cSleepPassesTime = World::Get().GetTransport().IsConnected() && World::Get().GetServerSettings().SleepEnabled;
    if (s_hooksReady && cSleepPassesTime != s_checkSkipped.load())
    {
        static const BSFixedString s_name("Sleep/Wait Menu");
        UI* pUI = UI::Get();
        if (pUI && !pUI->FindMenuByName(s_name))
            SetMainThreadCheckSkipped(cSleepPassesTime);
    }

    // Run on Main before its input/update jobs. Do not pause the world or touch VR pose tracking.
    static bool s_wasLocked = false;
    const bool cLocked = CalendarService::AreSleepControlsLocked();
    if (cLocked)
    {
        if (PlayerControls* pControls = PlayerControls::GetInstance())
        {
            // CanProcess blocks new events; clear values retained from a stick held when sleep began.
            pControls->Data.MoveInputVec = {};
            pControls->Data.PrevMoveVec = {};
            pControls->Data.LookInputVec = {};
            pControls->Data.PrevLookVec = {};
            pControls->Data.bAutoMove = false;
        }
        if (!s_wasLocked)
        {
            if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
                pPlayer->InterruptCast(true);
            spdlog::info("Sleep: party member sleeping, gameplay controls locked; head and hand tracking remain active");
        }
    }
    else if (s_wasLocked)
        spdlog::info("Sleep: party sleep ended, gameplay controls released");
    s_wasLocked = cLocked;
}

void RunDeferredSleepWaitAccept()
{
    if (!s_accept.HasPending())
        return;

    IMenu* pMenu = GetOpenSleepMenu();
    const auto cHours = s_accept.Take(pMenu);
    if (!cHours)
        return;

    // The native OK callback fades out and starts sleep. Calling it from the unpaused UI worker can
    // deadlock Main::Render, so invoke it here, before Main::Update starts its UI jobs, without pausing.
    alignas(16) uint8_t value[0x18]{};
    *reinterpret_cast<uint32_t*>(value + 0x08) = 3; // GFxValue::kNumber
    *reinterpret_cast<double*>(value + 0x10) = *cHours;
    alignas(16) uint8_t args[0x40]{};
    *reinterpret_cast<IMenu**>(args + 0x18) = pMenu;
    *reinterpret_cast<uint8_t**>(args + 0x28) = value;
    *reinterpret_cast<uint32_t*>(args + 0x30) = 1;
    RealOk(args);
}

static TiltedPhoques::Initializer s_sleepWaitHooks(
    []()
    {
        const auto cExe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* pCheck = reinterpret_cast<uint8_t*>(cExe + kMainThreadCheck);
        // The VR address library confirms the menu's ProcessMessage (SE 51614).
        // It has no entry for the OK callback or CanProcess, which are trusted once these checks confirm the executable.
        if (std::memcmp(pCheck, kMainThreadJump, sizeof(kMainThreadJump)) != 0 ||
            VersionDb::Get().FindAddressById(36564) != reinterpret_cast<void*>(cExe + kMainUpdate) ||
            VersionDb::Get().FindAddressById(51614) != reinterpret_cast<void*>(cExe + kProcessMessage))
        {
            spdlog::error("Sleep/Wait: unsupported main-thread check, Main::Update or ProcessMessage; leaving the menu paused");
            return;
        }
        for (const uintptr_t cCall : kPlayerSleepWaitCalls)
        {
            const auto* pCall = reinterpret_cast<const uint8_t*>(cExe + cCall);
            if (*pCall != 0xE8 || pCall + 5 + *reinterpret_cast<const int32_t*>(pCall + 1) != reinterpret_cast<const uint8_t*>(cExe + kSleepWaitStep))
            {
                spdlog::error("Sleep/Wait: unsupported player sleep step call; leaving the menu paused");
                return;
            }
        }
        if (*reinterpret_cast<const uint8_t*>(cExe + kMeleeSwingMenuCheck) != 0xE8)
            return;

        RealOk = reinterpret_cast<TOk*>(cExe + kOkCallback);
        RealProcessMessage = reinterpret_cast<TProcessMessage*>(cExe + kProcessMessage);
        RealCanProcess = reinterpret_cast<TCanProcess*>(cExe + kPlayerControlsCanProcess);
        TP_HOOK(&RealOk, HookOk);
        TP_HOOK(&RealProcessMessage, HookProcessMessage);
        TP_HOOK(&RealCanProcess, HookCanProcess);
        for (const uintptr_t cCall : kPlayerSleepWaitCalls)
            TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + cCall), RealSleepStep, &HookSleepStep);
        TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kMeleeSwingMenuCheck), RealMeleeMenuCheck, &HookMeleeMenuCheck);
        // The main-thread check is left as the game has it; UpdateSleepWatchControls skips it only where a sleep passes time.
        s_pMainThreadCheck = pCheck;
        s_hooksReady = true;
        spdlog::info("Sleep/Wait: unpaused menu hooks installed, confirmation deferred to Main::Update");
    });
#endif
