#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/GameSound.h>

#include <Forms/TESForm.h>
#include <Games/Primitives.h>

namespace GameSound
{
namespace
{
constexpr uint8_t kSoundDescriptorType = 0x80; // FormType SNDR (CommonLibVR-NG FormTypes.h)
constexpr uint32_t kSoundDescriptorInterface = 0x20; // BGSSoundDescriptorForm's BSISoundDescriptor

using TGetAudioManager = void*();
using TGetSoundHandle = bool(void* apManager, Handle& aHandle, void* apDescriptor, uint32_t aFlags);
using TSetPosition = bool(Handle* apHandle, NiPoint3 aPosition);
using TPlay = bool(Handle* apHandle);
using TStop = bool(Handle* apHandle);
} // namespace

bool Play(const uint32_t aDescriptorId, const NiPoint3* apAt, Handle* apOut) noexcept
{
    POINTER_SKYRIMSE(TGetAudioManager, s_getAudioManager, 66391, 66391);
    POINTER_SKYRIMSE(TGetSoundHandle, s_getSoundHandle, 66404, 66404);
    POINTER_SKYRIMSE(TSetPosition, s_setPosition, 66370, 66370);
    POINTER_SKYRIMSE(TPlay, s_play, 66355, 66355);
    if (!s_getAudioManager.Get() || !s_getSoundHandle.Get() || !s_setPosition.Get() || !s_play.Get())
        return false;

    TESForm* pForm = TESForm::GetById(aDescriptorId);
    if (!pForm || static_cast<uint8_t>(pForm->formType) != kSoundDescriptorType)
        return false;
    void* pManager = s_getAudioManager.Get()();
    if (!pManager)
        return false;
    Handle handle{};
    // 0x1A: the flags CommonLibVR-NG's BSAudioManager::Play passes.
    if (!s_getSoundHandle.Get()(pManager, handle, reinterpret_cast<uint8_t*>(pForm) + kSoundDescriptorInterface, 0x1A))
        return false;
    if (apAt)
        s_setPosition.Get()(&handle, *apAt);
    const bool cPlaying = s_play.Get()(&handle);
    if (apOut)
        *apOut = handle;
    return cPlaying;
}

bool Stop(Handle& aHandle) noexcept
{
    POINTER_SKYRIMSE(TStop, s_stop, 66358, 66358);
    if (aHandle.SoundId == 0xFFFFFFFF || !s_stop.Get())
        return false;
    const bool cStopped = s_stop.Get()(&aHandle);
    aHandle = Handle{};
    return cStopped;
}
} // namespace GameSound
