#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <openvr/openvr.h>

#include <CommCtrl.h>

#include <chrono>

// The game starts OpenVR once, through its import of VR_InitInternal2.
// When that fails (SteamVR or the headset not ready yet), the game carries on without VR and crashes at its first loading screen.
// The hook below stops there instead: it tells the player why, and starts OpenVR again when asked.
// OpenVR's VR_IsHmdPresent cannot replace the dialog: it answers true with no headset connected, and it starts SteamVR.

namespace
{
using TVR_InitInternal2 = uint32_t(__cdecl*)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
using TVR_GetVRInitErrorAsEnglishDescription = const char*(__cdecl*)(vr::EVRInitError);

TVR_InitInternal2 RealVR_InitInternal2 = nullptr;

constexpr int cTryAgain = 101;
constexpr int cCloseGame = 102;

std::string DescribeError(vr::EVRInitError aError) noexcept
{
    const HMODULE cOpenVR = GetModuleHandleW(L"openvr_api.dll");
    const auto cDescribe = cOpenVR ? reinterpret_cast<TVR_GetVRInitErrorAsEnglishDescription>(GetProcAddress(cOpenVR, "VR_GetVRInitErrorAsEnglishDescription")) : nullptr;
    const char* pText = cDescribe ? cDescribe(aError) : nullptr;
    return pText ? pText : "no description";
}

HRESULT CALLBACK DialogCallback(HWND aHwnd, UINT aNotification, WPARAM, LPARAM, LONG_PTR)
{
    if (aNotification == TDN_CREATED)
        SetWindowPos(aHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    return S_OK;
}

enum class Choice
{
    kTryAgain,
    kCloseGame,
    kNoDialog,
};

Choice AskToTryAgain(vr::EVRInitError aError) noexcept
{
    const std::string cDescription = DescribeError(aError);
    std::wstring content = L"SteamVR could not start the game in VR. This happens when the game starts before SteamVR or the headset is ready.\n\n"
                           L"Make sure SteamVR is running and shows your headset and both controllers, then choose Try again.\n\n"
                           L"OpenVR said: ";
    content += std::wstring(cDescription.begin(), cDescription.end());

    const TASKDIALOG_BUTTON cButtons[] = {{cTryAgain, L"Try again"}, {cCloseGame, L"Close the game"}};

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = L"urSovngarde";
    config.pszMainInstruction = L"VR could not start";
    config.pszContent = content.c_str();
    config.cButtons = static_cast<UINT>(std::size(cButtons));
    config.pButtons = cButtons;
    config.nDefaultButton = cTryAgain;
    config.pfCallback = &DialogCallback;

    int pressed = cCloseGame;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)))
        return Choice::kNoDialog;
    return pressed == cTryAgain ? Choice::kTryAgain : Choice::kCloseGame;
}

uint32_t __cdecl HookVR_InitInternal2(vr::EVRInitError* apError, vr::EVRApplicationType aType, const char* apStartupInfo)
{
    const auto cFirstAttempt = std::chrono::steady_clock::now();
    for (uint32_t attempt = 1;; ++attempt)
    {
        vr::EVRInitError error = vr::VRInitError_None;
        const uint32_t cToken = RealVR_InitInternal2(&error, aType, apStartupInfo);
        if (apError)
            *apError = error;

        if (error == vr::VRInitError_None)
        {
            const auto cWaited = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - cFirstAttempt).count();
            spdlog::info("VRStart: OpenVR started (attempt {}, {} s)", attempt, cWaited);
            return cToken;
        }

        spdlog::error("VRStart: OpenVR did not start (attempt {}): error {}, {}", attempt, static_cast<int>(error), DescribeError(error));
        switch (AskToTryAgain(error))
        {
        case Choice::kTryAgain: spdlog::info("VRStart: trying again"); break;
        case Choice::kCloseGame: spdlog::error("VRStart: the game closes instead of starting without VR"); exit(4);
        case Choice::kNoDialog:
            // Without the dialog the player cannot be asked, so the game goes on without VR as it would have.
            spdlog::error("VRStart: the dialog could not be shown; the game goes on without VR");
            return cToken;
        }
    }
}

TiltedPhoques::Initializer s_vrStartHook(
    []()
    {
        TP_HOOK_IAT(VR_InitInternal2, "openvr_api.dll");
        if (!RealVR_InitInternal2)
            spdlog::warn("VRStart: the game's import of VR_InitInternal2 was not found; a failed VR start is not caught");
    });
} // namespace

#endif
