#pragma once

#ifdef SKYRIMVR

#include <filesystem>

struct World;
struct UpdateEvent;

/**
 * @brief Lets the dashboard join friends through the urSovngarde launcher, which holds the codes and the relay tunnel.
 *
 * Two files sit beside connect.txt in %LOCALAPPDATA%\SkyrimTogetherVR, each with one writer.
 * launcher.json is the launcher's: its code while hosting, the friends hosting, and its answer to the last request.
 * It goes to the page as it is, and the page reads it.
 * launcher-request.json is ours: a join the page asks for.
 * The launcher answers in launcher.json and writes connect.txt, and the page then has VRConnectService connect to it.
 */
struct LauncherLinkService
{
    LauncherLinkService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~LauncherLinkService() noexcept = default;

    TP_NOCOPYMOVE(LauncherLinkService);

    // Asks the launcher to join a friend's code; the password stays out of the log.
    void RequestJoin(const std::string& acRequest, const std::string& acCode, const std::string& acName, const std::string& acPassword) noexcept;
    // The menu gave the join up: a request the launcher has not taken yet is withdrawn.
    void CancelRequest() noexcept;

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void SendState(const std::string& acText) const noexcept;

    World& m_world;
    std::filesystem::path m_folder;
    std::chrono::steady_clock::time_point m_nextLook{};
    std::filesystem::file_time_type m_seenWrite{};
    bool m_seenFile{false};

    entt::scoped_connection m_updateConnection;
};

#endif
