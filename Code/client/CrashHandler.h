#pragma once

//! Marks game code known to fault and recovered by an __except (see the crime alarm in Actor.cpp).
//! The crash handler ignores access violations inside it.
namespace CrashGuard
{
void Enter() noexcept;
void Leave() noexcept;
[[nodiscard]] bool Inside() noexcept;
} // namespace CrashGuard

//! Logs where the game is stuck when no frame has finished for 25 s, since a freeze leaves no crash report.
//! World::Update calls Beat() every frame.
//! Also logs the process's memory and thread count every 30 s, and where the game is in a frame that takes 200 ms or more.
namespace FreezeWatchdog
{
void Beat() noexcept;
void NoteLoadingScreen() noexcept;
void Start() noexcept;
} // namespace FreezeWatchdog

//! Where the player is, for the crash map.
//! PlayerService notes it once a second, and a crash logs the last note, so the handler reads no game memory.
namespace CrashPlace
{
//! Called from the game thread only; copies at most 383 characters.
void Note(const char* acpLine) noexcept;
//! The last line noted, or null before the first.
[[nodiscard]] const char* Last() noexcept;
} // namespace CrashPlace

//! Writes one line and flushes it at once, for code that runs while the process may be going down.
//! Does nothing once the logger has been destroyed at exit.
namespace CrashLog
{
void Line(const char* acpLine, spdlog::level::level_enum aLevel = spdlog::level::err) noexcept;
} // namespace CrashLog

class CrashHandler
{
    PVOID m_handler;
    static LPTOP_LEVEL_EXCEPTION_FILTER m_pUnhandled; // For remembering "original" UnhandledExceptionFilter

  public:
    CrashHandler();
    ~CrashHandler();

    static void RemovePreviousDump(std::filesystem::path path);
    static inline LPTOP_LEVEL_EXCEPTION_FILTER GetOriginalUnhandledExceptionFilter()
    {
        return m_pUnhandled;
    }
};
