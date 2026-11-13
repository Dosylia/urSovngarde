#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <Services/LauncherLinkService.h>
#include <Services/JsonText.h>
#include <Services/OverlayService.h>
#include <OverlayApp.hpp>

#include <Events/UpdateEvent.h>

#include <World.h>

#include <fstream>

namespace
{
constexpr auto kLookEvery = 1s;
constexpr size_t kMaxStateSize = 64 * 1024; // a few friends and one answer; anything bigger is not the launcher's file

bool IsNumber(const std::string& acText)
{
    return !acText.empty() && acText.size() <= 20 && std::all_of(acText.begin(), acText.end(), [](const char c) { return c >= '0' && c <= '9'; });
}
} // namespace

LauncherLinkService::LauncherLinkService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    char localAppData[MAX_PATH] = "";
    GetEnvironmentVariableA("LOCALAPPDATA", localAppData, sizeof(localAppData));
    m_folder = std::filesystem::path(localAppData) / "SkyrimTogetherVR";

    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&LauncherLinkService::OnUpdate>(this);
}

void LauncherLinkService::RequestJoin(const std::string& acRequest, const std::string& acCode, const std::string& acName, const std::string& acPassword) noexcept
{
    if (!IsNumber(acRequest) || acCode.empty() || acCode.size() > 32 || acName.size() > 128 || acPassword.size() > 128)
    {
        spdlog::warn("LauncherLink: a join request from the menu was malformed, not sent");
        return;
    }

    const std::string cText = fmt::format(
        "{{\"contract\":1,\"request\":{},\"action\":\"join\",\"code\":{},\"password\":{},\"name\":{}}}", acRequest, JsonQuoted(acCode), JsonQuoted(acPassword),
        JsonQuoted(acName));

    // Written aside and renamed over, so the launcher never reads half a request.
    std::error_code error;
    std::filesystem::create_directories(m_folder, error);
    const auto cPath = m_folder / "launcher-request.json";
    const auto cTemporary = m_folder / "launcher-request.json.tmp";
    // A request that could not be put in place is removed: it can hold a password.
    std::ofstream file(cTemporary, std::ios::binary | std::ios::trunc);
    file << cText;
    file.close();
    if (!file)
    {
        std::filesystem::remove(cTemporary, error);
        spdlog::warn("LauncherLink: could not write the join request for code {}", acCode);
        return;
    }
    std::filesystem::rename(cTemporary, cPath, error);
    if (error)
    {
        spdlog::warn("LauncherLink: could not put the join request for code {} in place: {}", acCode, error.message());
        std::filesystem::remove(cTemporary, error);
        return;
    }

    spdlog::info("LauncherLink: asked the launcher to join code {}{}", acCode, acPassword.empty() ? "" : ", with a password");
}

void LauncherLinkService::CancelRequest() noexcept
{
    // Once taken, the launcher has deleted the file, and the join can no longer be withdrawn.
    std::error_code error;
    if (std::filesystem::remove(m_folder / "launcher-request.json", error))
        spdlog::info("LauncherLink: the menu gave the join up before the launcher took it");
}

void LauncherLinkService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextLook)
        return;
    m_nextLook = cNow + kLookEvery;

    // The launcher rewrites its file every few seconds while it runs, so the page also learns when it stops.
    std::error_code error;
    const auto cPath = m_folder / "launcher.json";
    const auto cWrite = std::filesystem::last_write_time(cPath, error);
    if (error)
    {
        if (m_seenFile)
        {
            spdlog::info("LauncherLink: the launcher's state file is gone");
            m_seenFile = false;
            SendState({});
        }
        return;
    }
    if (m_seenFile && cWrite == m_seenWrite)
        return;

    // A read that fails while the launcher renames its file is tried again at the next look.
    std::ifstream file(cPath, std::ios::binary);
    if (!file)
        return;
    std::string text(kMaxStateSize + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(file.gcount()));
    if (text.size() > kMaxStateSize)
        return;

    if (!m_seenFile)
        spdlog::info("LauncherLink: the launcher's state file is there, the menu can join friends through it");
    m_seenFile = true;
    m_seenWrite = cWrite;
    SendState(text);
}

void LauncherLinkService::SendState(const std::string& acText) const noexcept
{
    auto* pApp = m_world.ctx().at<OverlayService>().GetOverlayApp();
    if (!pApp)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetString(0, acText);
    pApp->ExecuteAsync("launcherState", pArguments);
}

#endif
