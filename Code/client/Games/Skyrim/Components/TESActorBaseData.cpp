#include <TiltedOnlinePCH.h>

#include <Components/TESActorBaseData.h>
#include <Forms/TESActorBase.h>

TESActorBase* TESActorBaseData::CreateTemplateActorBase(TESActorBase* apOriginalBase, TESActorBase* apTemplateBase) noexcept
{
    using TCreateTemplateActorBase = decltype(TESActorBaseData::CreateTemplateActorBase);
    POINTER_SKYRIMSE(TCreateTemplateActorBase, s_CreateTemplateActorBase, 14375, 14375);
    return s_CreateTemplateActorBase.Get()(apOriginalBase, apTemplateBase);
}
