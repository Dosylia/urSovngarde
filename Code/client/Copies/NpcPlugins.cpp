#include <TiltedOnlinePCH.h>

#include <Copies/NpcPlugins.h>

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

void AddFile(const Mod* apMod, const Mod** apFiles, size_t& aCount) noexcept
{
    for (size_t i = 0; i < aCount; ++i)
        if (apFiles[i] == apMod)
            return;
    if (aCount < kMaxFiles)
        apFiles[aCount++] = apMod;
}

// Kept in its own function: MSVC allows no __try where C++ objects need unwinding.
// A fault reading the game's records stops it, and nothing is sent.
bool CollectFiles(const Mod** apFiles, size_t& aCount) noexcept
{
    __try
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
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The names are copied under the same guard: a wrong record layout would give pointers that are not files.
bool CopyNames(const Mod* const* apFiles, const size_t aCount, char (*apNames)[kNameSize + 1]) noexcept
{
    __try
    {
        for (size_t i = 0; i < aCount; ++i)
        {
            std::memcpy(apNames[i], apFiles[i]->filename, kNameSize);
            apNames[i][kNameSize] = '\0';
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

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
