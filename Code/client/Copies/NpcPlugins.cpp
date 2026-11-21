#include <TiltedOnlinePCH.h>

#include <Copies/NpcPlugins.h>

#include <CrashHandler.h>
#include <Games/TES.h>
#include <Forms/TESForm.h>
#include <Structs/PluginList.h>

#include <algorithm>
#include <cstring>

namespace
{
// The game keeps one array of loaded records per form type, 0x18 bytes each, from 0x10.
constexpr size_t kFormArraysOffset = 0x10;
constexpr size_t kFormArraySize = 0x18;
// A record's files: the one that adds it and each that edits it (TESForm::sourceFiles).
constexpr size_t kFormFilesOffset = 0x08;
constexpr size_t kMaxFiles = 1024;
constexpr size_t kNameSize = sizeof(Mod::filename);

struct FileArray
{
    Mod** pData;
    uint32_t Size;
};

/**
 * Add a plugin file to the list unless it is already there.
 * A file past the list's room is left out.
 *
 * @param apMod The plugin file.
 * @param apFiles The list, with room for kMaxFiles files.
 * @param aCount How many files the list holds; raised by one when the file is added.
 */
void AddFile(const Mod* apMod, const Mod** apFiles, size_t& aCount) noexcept
{
    for (size_t i = 0; i < aCount; ++i)
        if (apFiles[i] == apMod)
            return;
    if (aCount < kMaxFiles)
        apFiles[aCount++] = apMod;
}

/**
 * Add every plugin file that adds or edits an NPC or levelled NPC list record to the list, each once.
 * It has no guard of its own: CollectFiles calls it inside one.
 *
 * @param apFiles The list, with room for kMaxFiles files.
 * @param aCount How many files the list holds; raised for each new file.
 * @return True when the game's records were walked; false when the game has no record manager yet.
 */
bool AddNpcRecordFiles(const Mod** apFiles, size_t& aCount) noexcept
{
    const auto* pManager = reinterpret_cast<const uint8_t*>(ModManager::Get());
    if (!pManager)
        return false;
    for (const FormType cType : {FormType::Npc, FormType::LeveledCharacter})
    {
        const auto* pForms = reinterpret_cast<const GameArray<TESForm*>*>(pManager + kFormArraysOffset + static_cast<size_t>(cType) * kFormArraySize);
        for (uint32_t i = 0; i < pForms->length; ++i)
        {
            const TESForm* pForm = pForms->data[i];
            if (!pForm)
                continue;
            const auto* pFiles = *reinterpret_cast<const FileArray* const*>(reinterpret_cast<const uint8_t*>(pForm) + kFormFilesOffset);
            if (!pFiles || !pFiles->pData)
                continue;
            for (uint32_t f = 0; f < pFiles->Size; ++f)
                if (const Mod* pMod = pFiles->pData[f])
                    AddFile(pMod, apFiles, aCount);
        }
    }
    return true;
}

/**
 * List every plugin file that adds or edits an NPC or levelled NPC list record, without crashing on a broken record.
 * The crash handler is told a fault here is expected, so it neither reports a crash nor uses up the session's crash report.
 * Kept in its own function: MSVC allows no __try where C++ objects need unwinding.
 *
 * @param apFiles Receives the files, with room for kMaxFiles.
 * @param aCount How many files the list holds; after a fault, those found before it.
 * @return True when every record was read; false when the game has no record manager yet or a read faulted.
 */
bool CollectFiles(const Mod** apFiles, size_t& aCount) noexcept
{
    bool read = false;
    CrashGuard::Enter();
    __try
    {
        read = AddNpcRecordFiles(apFiles, aCount);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // The fault is expected here, and the result stays false.
    }
    CrashGuard::Leave();
    return read;
}

/**
 * Copy each plugin file's name into a text of its own, without crashing on a pointer that is not a file.
 * A wrong record layout would give such pointers, so the crash handler is told a fault here is expected.
 *
 * @param apFiles The plugin files, as CollectFiles listed them.
 * @param aCount How many files the list holds.
 * @param apNames Receives one name per file, ended after at most kNameSize characters.
 * @return True when every name was copied; false when a read faulted.
 */
bool CopyNames(const Mod* const* apFiles, const size_t aCount, char (*apNames)[kNameSize + 1]) noexcept
{
    bool copied = false;
    CrashGuard::Enter();
    __try
    {
        for (size_t i = 0; i < aCount; ++i)
        {
            std::memcpy(apNames[i], apFiles[i]->filename, kNameSize);
            apNames[i][kNameSize] = '\0';
        }
        copied = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // The fault is expected here, and the result stays false.
    }
    CrashGuard::Leave();
    return copied;
}
} // namespace

/**
 * List the plugins that add or change NPCs or levelled NPC lists, sorted by name.
 * Read from the game's loaded records on the first call, then kept; nothing is listed when a read faults.
 *
 * @return The plugins' file names; empty when the game's records could not be read.
 */
const Vector<String>& NpcPlugins::Get() noexcept
{
    static Vector<String> s_plugins;
    static bool s_read = false;
    if (s_read)
        return s_plugins;
    s_read = true;

    static const Mod* s_files[kMaxFiles];
    static char s_names[kMaxFiles][kNameSize + 1];
    size_t count = 0;
    if (!CollectFiles(s_files, count) || !CopyNames(s_files, count, s_names))
    {
        spdlog::error("NpcPlugins: the game's NPC records could not be read ({} plugins found before the fault); nothing is sent", count);
        return s_plugins;
    }

    for (size_t i = 0; i < count; ++i)
        s_plugins.push_back(String(s_names[i]));
    std::sort(s_plugins.begin(), s_plugins.end());

    spdlog::info("NpcPlugins: {} plugins add or change NPCs or levelled NPC lists: {}", s_plugins.size(), PluginList::Joined(s_plugins).c_str());
    return s_plugins;
}
