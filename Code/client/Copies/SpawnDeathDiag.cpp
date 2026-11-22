#include <TiltedOnlinePCH.h>

#include <Copies/SpawnDeathDiag.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/LifeStates.h>
#include <Helpers/ModuleImage.h>
#ifdef SKYRIMVR
#include <Games/Skyrim/Watches/GameStack.h>

namespace
{
// Frames kept of the stack that killed the copy.
constexpr USHORT kStackFrames = 24;
} // namespace
#endif

namespace SpawnDeathDiag
{
/**
 * Note a life state the game gives an actor, and log it with its stack when it is the first death of a copy still waiting for its 3D step.
 * Called from the game's Actor::SetLifeState, before the state is set; an actor that is no longer a copy, or already dead, is left out.
 *
 * @param apActor The actor.
 * @param aState The life state it is given.
 * @param apCaller The address Actor::SetLifeState returns to, named where no stack can be read (flat).
 */
void NoteLifeState(Actor* apActor, const uint32_t aState, const void* apCaller) noexcept
{
    ActorExtension* pExtension = apActor ? apActor->GetExtension() : nullptr;
    if (!pExtension || !pExtension->AwaitingFirst3D || (aState != LifeStates::kDying && aState != LifeStates::kDead))
        return;
    pExtension->AwaitingFirst3D = false;
    if (!pExtension->IsRemote() || apActor->IsDead())
        return;

#ifdef SKYRIMVR
    void* frames[kStackFrames];
    const USHORT cDepth = GameStack::Capture(frames, kStackFrames);
    spdlog::warn("SpawnDeathDiag: copy {:X} given life state {} before its 3D step (3D {}); the stack that killed it:\n{}", apActor->formID, aState,
                 apActor->GetNiNode() != nullptr, GameStack::Describe(frames, cDepth));
#else
    spdlog::warn("SpawnDeathDiag: copy {:X} given life state {} before its 3D step (3D {}); the game's life state set from {}", apActor->formID, aState,
                 apActor->GetNiNode() != nullptr, ModuleImage::Where(apCaller));
#endif
}
} // namespace SpawnDeathDiag
