#include <TiltedOnlinePCH.h>

#include <TiltedOnlineApp.h>

#include <DInputHook.hpp>
#include <dinput.h>
#include <WindowsHook.hpp>

#include <PluginCalls.h>
#include <World.h>
#include <ScriptExtender.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Systems/RenderSystemD3D11.h>

#include <Services/OverlayService.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>

#include <NvidiaUtil.h>

#include <tlhelp32.h>

using TiltedPhoques::Debug;

namespace
{
// Client instances use tp_client.log, tp_client_instance_2.log, tp_client_instance_3.log, etc.
std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> CreateClientLogSink(const std::filesystem::path& acLogPath)
{
    std::string filename = "tp_client.log";
    for (uint32_t instance = 1;; ++instance)
    {
        const auto mutexName = fmt::format("SkyrimTogether.ClientLog.Instance_{}", instance);
        HANDLE pMutex = CreateMutexA(nullptr, FALSE, mutexName.c_str());
        const DWORD error = GetLastError();

        if (!pMutex)
        {
            // Never fall back to a filename that another client may be writing to.
            spdlog::warn("Failed to reserve a client log slot (error {}); using a process-specific log", error);
            filename = fmt::format("tp_client_pid_{}.log", GetCurrentProcessId());
            break;
        }

        if (error == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(pMutex);
            continue;
        }

        // Keep this slot's mutex handle open until process exit, like ArrangeGameWindows.
        if (instance > 1)
            filename = fmt::format("tp_client_instance_{}.log", instance);
        break;
    }
    return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(acLogPath / filename, 1048576 * 5, 3);
}
}

// Logs whether EngineFixesCustom.toml switches off the two Engine Fixes 7 guards that cannot work under the launcher.
// Called at the first update, because the preloader loads EngineFixes.dll after this app starts.
// The working directory is the game's.
static void LogEngineFixes7() noexcept
{
    if (GetModuleHandleW(L"EngineFixes.dll"))
    {
        std::ifstream custom("Data\\SKSE\\Plugins\\EngineFixesCustom.toml");
        uint32_t off = 0;
        for (std::string line; std::getline(custom, line);)
        {
            line.erase(std::remove_if(line.begin(), line.end(), [](const char c) { return c == ' ' || c == '\t'; }), line.end());
            if (line.rfind("bCullingFreedObjectCrash=false", 0) == 0 || line.rfind("bSceneGraphDetachFreedCrash=false", 0) == 0)
                ++off;
        }
        if (off == 2)
            spdlog::info("Engine Fixes 7 is loaded, with the two guards that cannot work under this launcher off (EngineFixesCustom.toml)");
        else
            spdlog::error("Engine Fixes 7 is loaded, but only {} of its two guards that cannot work under this launcher is off in "
                          "EngineFixesCustom.toml: with bCullingFreedObjectCrash on, the view is black",
                          off);
    }
}

// A plugin's version as Crash Logger shows it: the file's ProductVersion text, else the version it gives SKSE.
// The file's numeric version comes last, because some are placeholders (DynDOLOD's is 666.666.666.666).
static std::string PluginVersionOf(const MODULEENTRY32W& acEntry) noexcept
{
    std::string data;
    DWORD handle = 0;
    if (const DWORD cSize = GetFileVersionInfoSizeW(acEntry.szExePath, &handle))
    {
        data.resize(cSize);
        if (!GetFileVersionInfoW(acEntry.szExePath, 0, cSize, data.data()))
            data.clear();
    }

    if (!data.empty())
    {
        struct Translation
        {
            WORD Language;
            WORD CodePage;
        };
        Translation* pTranslations = nullptr;
        UINT size = 0;
        std::vector<Translation> translations;
        if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&pTranslations), &size) && pTranslations)
            translations.assign(pTranslations, pTranslations + size / sizeof(Translation));
        translations.push_back({0x0409, 0x04B0});
        translations.push_back({0x0409, 0x04E4});

        for (const auto& cTranslation : translations)
        {
            wchar_t key[64];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductVersion", cTranslation.Language, cTranslation.CodePage);
            wchar_t* pText = nullptr;
            UINT length = 0;
            if (VerQueryValueW(data.data(), key, reinterpret_cast<LPVOID*>(&pText), &length) && pText && length > 1 && *pText)
            {
                char text[256]{};
                if (!WideCharToMultiByte(CP_UTF8, 0, pText, -1, text, sizeof(text) - 1, nullptr, nullptr))
                    continue;
                // The log line separates plugins with commas, and old version texts are written "1, 0, 0, 1".
                std::string version;
                for (const char* p = text; *p; ++p)
                {
                    if (*p == ',')
                        version += '.';
                    else if (*p != ' ' || (p > text && p[-1] != ','))
                        version += *p;
                }
                return version;
            }
        }
    }

    struct SksePluginVersion
    {
        uint32_t DataVersion;
        uint32_t PluginVersion;
    };
    if (const auto* pVersion = reinterpret_cast<const SksePluginVersion*>(GetProcAddress(acEntry.hModule, "SKSEPlugin_Version")))
    {
        const uint32_t cPacked = pVersion->PluginVersion;
        std::string version = fmt::format("{}.{}.{}", cPacked >> 24, (cPacked >> 16) & 0xFF, (cPacked >> 4) & 0xFFF);
        if (cPacked & 0xF)
            version += fmt::format(".{}", cPacked & 0xF);
        return version;
    }

    VS_FIXEDFILEINFO* pInfo = nullptr;
    UINT infoSize = 0;
    if (!data.empty() && VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&pInfo), &infoSize) && pInfo &&
        infoSize >= sizeof(VS_FIXEDFILEINFO))
        return fmt::format("{}.{}.{}.{}", pInfo->dwFileVersionMS >> 16, pInfo->dwFileVersionMS & 0xFFFF, pInfo->dwFileVersionLS >> 16, pInfo->dwFileVersionLS & 0xFFFF);

    return "no version";
}

// Logs every loaded SKSE plugin with its version, so a crash report shows which build of each plugin was there.
// Called at the first update, once SKSE has loaded its plugins.
// Only file names are logged: a mod manager's path holds the player's folders.
static void LogPluginVersions() noexcept
{
    const HANDLE cSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (cSnapshot == INVALID_HANDLE_VALUE)
        return;

    std::string list;
    uint32_t count = 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32FirstW(cSnapshot, &entry); more; more = Module32NextW(cSnapshot, &entry))
    {
        std::wstring path = entry.szExePath;
        std::transform(path.begin(), path.end(), path.begin(), towlower);
        if (path.find(L"\\skse\\plugins\\") == std::wstring::npos)
            continue;

        char name[MAX_MODULE_NAME32 * 3 + 1]{};
        WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, name, sizeof(name) - 1, nullptr, nullptr);
        list += fmt::format("{}{} {}", count ? ", " : "", name, PluginVersionOf(entry));
        ++count;
    }
    CloseHandle(cSnapshot);

    spdlog::info("SKSE plugins ({}): {}", count, count ? list : "none");
}

#ifdef SKYRIMVR
// RenderPassWatch.cpp: hooks the list draw the first time the map is open, after every SKSE plugin has loaded.
void InstallDrawListCheck() noexcept;
#endif

TiltedOnlineApp::TiltedOnlineApp()
{
    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    auto logPath = TiltedPhoques::GetPath() / "logs";

    std::error_code ec;
    create_directory(logPath, ec);

    auto rotatingLogger = CreateClientLogSink(logPath);
    // rotatingLogger->set_level(spdlog::level::debug);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // The console blocks the game thread and nobody reads it in a headset, so it only gets warnings and errors.
    console->set_level(spdlog::level::warn);
    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console, rotatingLogger});
    logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] %$ %v");
    spdlog::flush_every(std::chrono::seconds(1));
    // A game that ends at once takes the last second of its log with it, unless what matters is already on disk.
    logger->flush_on(spdlog::level::warn);
    set_default_logger(logger);

    spdlog::info("Skyrim Together client, build " BUILD_BRANCH "@" BUILD_COMMIT);

#ifdef SKYRIMVR
    // Logged here because the address database loads before the logger exists.
    // Each is an id the installed VR Address Library defines differently; ours is used.
    for (const auto& conflict : VersionDb::Get().GetOverrideConflicts())
        spdlog::warn("Address id {} differs: the VR Address Library says {:X}, this client uses its own {:X}", conflict.Id, conflict.Library, conflict.Ours);

    // Other plugins still read the player's library, and an older one lacks ids they need.
    const auto& cDb = VersionDb::Get();
    if (cDb.GetLibraryIdCount() == 0)
        spdlog::warn("VR Address Library: its file holds no ids");
    else
        spdlog::info("VR Address Library {}: {} ids", cDb.GetLibraryVersion().empty() ? "of unknown version" : cDb.GetLibraryVersion(), cDb.GetLibraryIdCount());
#endif
}

TiltedOnlineApp::~TiltedOnlineApp() = default;

void* TiltedOnlineApp::GetMainAddress() const
{
    POINTER_SKYRIMSE(void, winMain, 36544, 36544);

    return winMain.GetPtr();
}

bool TiltedOnlineApp::BeginMain()
{
    World::Create();
    World::Get().ctx().at<DiscordService>().Init();
    World::Get().ctx().emplace<RenderSystemD3D11>(World::Get().ctx().at<OverlayService>(), World::Get().ctx().at<ImguiService>());

#ifdef SKYRIMVR
    // Nothing else starts SKSE VR; it must load here, not in RunTiltedInit (see main.cpp).
    LoadScriptExtender();
#endif

    // TODO: Figure out a way to un-blacklist NvCamera64.dll (see DllBlocklist.cpp). Then this hack can be removed
    if (IsNvidiaOverlayLoaded())
        ApplyNvidiaFix();

    return true;
}

bool TiltedOnlineApp::EndMain()
{
    UninstallHooks();
    if (m_pDevice)
        m_pDevice->Release();

    return true;
}

void TiltedOnlineApp::Update()
{
    // Reverting a change that used to be here to disable bUseFaceGenPreprocessedHeads==true (which is 
    // the default) handling. Extensive testing over months by multiple parties showed that enabling 
    // the flag introduces no issues WITH PROPERLY GENERATED CHARACTERS (in-game character generation 
    // or showracemenu). The shortcut of  "coc riverwood" from the main menu skips proper character generation.
    // 
    // Plus, having it on  has some benefits like helping with neck seams. Comment to avoid revisiting.
    // 
    // There are still some issues to track down, like hair color and maybe face tint not syncing correctly,
    // but they are unrelated and unchanged by this flag.
    // 
 
    // Make sure the window stays active
    POINTER_SKYRIMSE(uint32_t, bAlwaysActive, 380768, 380768);

    *bAlwaysActive = 1;

    static bool s_engineFixesLogged = false;
    if (!s_engineFixesLogged)
    {
        s_engineFixesLogged = true;
        LogEngineFixes7();
        LogPluginVersions();
    }
#ifdef SKYRIMVR
    InstallDrawListCheck();
#endif

    World::Get().Update();
    RunPluginCalls();
}

bool TiltedOnlineApp::Attach()
{
    TiltedPhoques::Debug::OnAttach();

    // TiltedPhoques::Nop(0x1405D3FA1, 6);
    return true;
}

bool TiltedOnlineApp::Detach()
{
    TiltedPhoques::Debug::OnDetach();
    return true;
}

void TiltedOnlineApp::InstallHooks2()
{
    TiltedPhoques::Initializer::RunAll();

    TiltedPhoques::DInputHook::Install();
    TiltedPhoques::DInputHook::Get().SetToggleKeys({DIK_F2, DIK_RCONTROL});
}

void TiltedOnlineApp::UninstallHooks()
{
}

void TiltedOnlineApp::ApplyNvidiaFix() noexcept
{
    auto d3dFeatureLevelOut = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = CreateEarlyDxDevice(&m_pDevice, &d3dFeatureLevelOut);
    if (FAILED(hr))
        spdlog::error("D3D11CreateDevice failed. Detected an NVIDIA GPU, error code={0:x}", hr);

    if (d3dFeatureLevelOut < D3D_FEATURE_LEVEL_11_0)
        spdlog::warn("Unexpected D3D11 feature level detected (< 11.0), may cause issues");
}
