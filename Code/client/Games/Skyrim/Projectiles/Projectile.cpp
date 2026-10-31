#include "Projectile.h"
#include <Games/Skyrim/Forms/TESObjectWEAP.h>
#include <Games/Skyrim/Forms/MagicItem.h>
#include <Games/Skyrim/Forms/TESAmmo.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <World.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Games/Skyrim/Forms/TESObjectCELL.h>
#include <Forms/SpellItem.h>
#include <PatchCheck.h>
#ifdef SKYRIMVR
#include <Games/Skyrim/VRBodySync.h>
#include <PlayerCharacter.h>
#include <atomic>
#endif

TP_THIS_FUNCTION(TLaunch, BSPointerHandle<Projectile>*, BSPointerHandle<Projectile>, Projectile::LaunchData& arData);
static TLaunch* RealLaunch = nullptr;

BSPointerHandle<Projectile>* Projectile::Launch(BSPointerHandle<Projectile>* apResult, LaunchData& apLaunchData) noexcept
{
    BSPointerHandle<Projectile>* result = TiltedPhoques::ThisCall(RealLaunch, apResult, apLaunchData);

    TP_ASSERT(result, "No projectile handle returned.");
    if (!result)
    {
        spdlog::error("No projectile handle returned.");
        return nullptr;
    }

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    TP_ASSERT(pProjectile, "No projectile found.");
    if (!pProjectile)
    {
        spdlog::error("No projectile found.");
        return nullptr;
    }

    pProjectile->fPower = apLaunchData.fPower;

    return result;
}

// The form id behind a LaunchData pointer, or 0 on VR unless that id maps back to this pointer.
// A wrong layout then costs an unsynced projectile, not a crash.
static uint32_t ValidFormId(const void* apForm) noexcept
{
    if (!apForm)
        return 0;

#ifdef SKYRIMVR
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(apForm, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    if (static_cast<const uint8_t*>(apForm) + sizeof(TESForm) > static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize)
        return 0;
#endif

    const auto* pForm = static_cast<const TESForm*>(apForm);
    const uint32_t formId = pForm->formID;

#ifdef SKYRIMVR
    if (TESForm::GetById(formId) != pForm)
        return 0;
#endif

    return formId;
}

#ifdef SKYRIMVR
// A held spell is cast again on its caster's copy, and each frame a flame or a beam takes its shooter's aim.
// A copy's aim is its body's. The real spell follows the hand, as turned by whatever spell mods the caster runs.
// So the real one's angles travel with the VR pose, and these calls give them to the copy's.
// They are the four calls to the actor's aim in the two updates (VR 0x140768570 and 0x14075E720), heading then pitch in each.
// The real angles are read where each update turns the player's own spell to its hand: a launch's angles can be the game's aim assist.
constexpr uintptr_t kAimHeading = 0x6104F0;
constexpr uintptr_t kAimPitch = 0x610590;
constexpr uintptr_t kFlameHeadingCall = 0x76869B;
constexpr uintptr_t kFlamePitchCall = 0x7686AE;
constexpr uintptr_t kBeamHeadingCall = 0x75E8CF;
constexpr uintptr_t kBeamPitchCall = 0x75E8E2;
constexpr uintptr_t kHandAim = 0x778ED0;
constexpr uintptr_t kFlameHandAimCall = 0x76875B;
constexpr uintptr_t kBeamHandAimCall = 0x75E987;

using TActorAim = float(Actor*);
static TActorAim* RealFlameHeading = nullptr;
static TActorAim* RealFlamePitch = nullptr;
static TActorAim* RealBeamHeading = nullptr;
static TActorAim* RealBeamPitch = nullptr;

using THandAim = void(Projectile*, const void*);
static THandAim* RealFlameHandAim = nullptr;
static THandAim* RealBeamHandAim = nullptr;

// Whether a second has passed since this said true last. The updates can run on any thread.
static bool SecondPassed(std::atomic<int64_t>& aNext) noexcept
{
    const int64_t cNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t next = aNext.load(std::memory_order_relaxed);
    return cNow >= next && aNext.compare_exchange_strong(next, cNow + 1000);
}

static bool OwnerSpellAim(const Actor* apShooter, float& aZAngle, float& aXAngle) noexcept
{
    const ActorExtension* pExtension = const_cast<Actor*>(apShooter)->GetExtension();
    return pExtension && pExtension->IsRemotePlayer() && VRBodySync::CopySpellAim(apShooter->formID, aZAngle, aXAngle);
}

// How far the copy's held spell passes from this player, so that a session's log says it.
static void SayCopySpellAim(const Actor* apShooter, const float aZAngle, const float aXAngle) noexcept
{
    static std::atomic<int64_t> s_next{0};
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || !SecondPassed(s_next))
        return;
    const glm::vec3 cWay = glm::vec3(pPlayer->position) - glm::vec3(apShooter->position);
    const float cToPlayer = std::atan2(cWay.x, cWay.y);
    spdlog::info("SpellAimDiag: copy {:X}'s held spell goes heading {:.1f}, pitch {:.1f}; this player is {:.1f} degrees off it, {:.0f} units away", apShooter->formID,
                 glm::degrees(aZAngle), glm::degrees(aXAngle), glm::degrees(std::remainder(cToPlayer - aZAngle, glm::two_pi<float>())), glm::length(cWay));
}

static float HookFlameHeading(Actor* apShooter)
{
    float zAngle = 0.f, xAngle = 0.f;
    if (!OwnerSpellAim(apShooter, zAngle, xAngle))
        return RealFlameHeading(apShooter);
    SayCopySpellAim(apShooter, zAngle, xAngle);
    return zAngle;
}

static float HookFlamePitch(Actor* apShooter)
{
    float zAngle = 0.f, xAngle = 0.f;
    return OwnerSpellAim(apShooter, zAngle, xAngle) ? xAngle : RealFlamePitch(apShooter);
}

static float HookBeamHeading(Actor* apShooter)
{
    float zAngle = 0.f, xAngle = 0.f;
    if (!OwnerSpellAim(apShooter, zAngle, xAngle))
        return RealBeamHeading(apShooter);
    SayCopySpellAim(apShooter, zAngle, xAngle);
    return zAngle;
}

static float HookBeamPitch(Actor* apShooter)
{
    float zAngle = 0.f, xAngle = 0.f;
    return OwnerSpellAim(apShooter, zAngle, xAngle) ? xAngle : RealBeamPitch(apShooter);
}

static void NoteHandAim(const Projectile* apThis) noexcept
{
    VRBodySync::NoteSpellAim(apThis->rotation.z, apThis->rotation.x);

    static std::atomic<int64_t> s_next{0};
    if (const PlayerCharacter* pPlayer = PlayerCharacter::Get(); pPlayer && SecondPassed(s_next))
        spdlog::info("SpellAimDiag: this player's held spell goes heading {:.1f}, pitch {:.1f}; its body at heading {:.1f}", glm::degrees(apThis->rotation.z),
                     glm::degrees(apThis->rotation.x), glm::degrees(pPlayer->rotation.z));
}

static void HookFlameHandAim(Projectile* apThis, const void* apHandMatrix)
{
    RealFlameHandAim(apThis, apHandMatrix);
    NoteHandAim(apThis);
}

static void HookBeamHandAim(Projectile* apThis, const void* apHandMatrix)
{
    RealBeamHandAim(apThis, apHandMatrix);
    NoteHandAim(apThis);
}

static void HookHeldSpellAim() noexcept
{
    const auto cExe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto cCallsTo = [cExe](const uintptr_t aCall, const uintptr_t aTarget)
    {
        const auto* pCall = reinterpret_cast<const uint8_t*>(cExe + aCall);
        return *pCall == 0xE8 && reinterpret_cast<uintptr_t>(pCall + 5 + *reinterpret_cast<const int32_t*>(pCall + 1)) == cExe + aTarget;
    };
    if (!cCallsTo(kFlameHeadingCall, kAimHeading) || !cCallsTo(kFlamePitchCall, kAimPitch) || !cCallsTo(kBeamHeadingCall, kAimHeading) || !cCallsTo(kBeamPitchCall, kAimPitch) ||
        !cCallsTo(kFlameHandAimCall, kHandAim) || !cCallsTo(kBeamHandAimCall, kHandAim))
    {
        spdlog::error("Projectile: the flame's or the beam's aim calls are not the expected code; another player's held spell follows its copy's body");
        return;
    }
    TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kFlameHeadingCall), RealFlameHeading, &HookFlameHeading);
    TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kFlamePitchCall), RealFlamePitch, &HookFlamePitch);
    TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kBeamHeadingCall), RealBeamHeading, &HookBeamHeading);
    TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kBeamPitchCall), RealBeamPitch, &HookBeamPitch);
    TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kFlameHandAimCall), RealFlameHandAim, &HookFlameHandAim);
    TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(cExe + kBeamHandAimCall), RealBeamHandAim, &HookBeamHandAim);
}
#endif

BSPointerHandle<Projectile>* TP_MAKE_THISCALL(HookLaunch, BSPointerHandle<Projectile>, Projectile::LaunchData& arData)
{
    // sync concentration spells through spell cast sync, the rest through projectile sync
    if (arData.pSpell)
    {
        if (auto* pSpell = Cast<SpellItem>(arData.pSpell))
        {
            if (pSpell->eCastingType == MagicSystem::CastingType::CONCENTRATION)
            {
                return TiltedPhoques::ThisCall(RealLaunch, apThis, arData);
            }
        }
    }

    if (arData.pShooter)
    {
        Actor* pActor = Cast<Actor>(arData.pShooter);
        if (pActor)
        {
            ActorExtension* pExtendedActor = pActor->GetExtension();
            // Null for most creatures.
            if (pExtendedActor && pExtendedActor->IsRemote())
            {
#ifdef SKYRIMVR
                // The owner syncs this projectile, so the local copy must go.
                // VR's LaunchSpell does not null-check the result, so launch it and delete it on the main thread.
                auto* pResult = TiltedPhoques::ThisCall(RealLaunch, apThis, arData);
                if (pResult && pResult->handle.iBits && entt::locator<World>::has_value())
                {
                    World::Get().GetRunner().Queue(
                        [handle = pResult->handle.iBits]()
                        {
                            if (TESObjectREFR* pObject = TESObjectREFR::GetByHandle(handle))
                            {
                                pObject->Disable();
                                pObject->Delete();
                            }
                        });
                }
                return pResult;
#else
                apThis->handle.iBits = 0;
                return apThis;
#endif
            }
        }
    }

    ProjectileLaunchedEvent Event{};
    Event.Origin = arData.Origin;
    Event.ProjectileBaseID = ValidFormId(arData.pProjectileBase);
    Event.ShooterID = ValidFormId(arData.pShooter);
    Event.WeaponID = ValidFormId(arData.pFromWeapon);
    Event.AmmoID = ValidFormId(arData.pFromAmmo);
    Event.ZAngle = arData.fZAngle;
    Event.XAngle = arData.fXAngle;
    Event.YAngle = arData.fYAngle;
    Event.ParentCellID = ValidFormId(arData.pParentCell);
    Event.SpellID = ValidFormId(arData.pSpell);

    // TEMPORARY: logs which ids resolved, for the first ten launches and every spell launch.
    // Tools/VR/session-report.py reads these lines.
    static uint32_t s_launchesLogged = 0;
    if (s_launchesLogged < 10 || Event.SpellID != 0)
    {
        ++s_launchesLogged;
        spdlog::info("Projectile launch: shooter {:X}, base {:X}, weapon {:X}, ammo {:X}, cell {:X}, spell {:X}", Event.ShooterID, Event.ProjectileBaseID,
                     Event.WeaponID, Event.AmmoID, Event.ParentCellID, Event.SpellID);
    }
    Event.CastingSource = arData.eCastingSource;
    Event.UnkBool1 = arData.bUnkBool1;
    Event.Area = arData.iArea;
    Event.Power = arData.fPower;
    Event.Scale = arData.fScale;
    Event.AlwaysHit = arData.bAlwaysHit;
    Event.NoDamageOutsideCombat = arData.bNoDamageOutsideCombat;
    Event.AutoAim = arData.bAutoAim;
    Event.UnkBool2 = arData.bUnkBool2;
    Event.DeferInitialization = arData.bDeferInitialization;
    Event.ForceConeOfFire = arData.bForceConeOfFire;

    auto result = TiltedPhoques::ThisCall(RealLaunch, apThis, arData);

    TP_ASSERT(result, "No projectile handle returned.");

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    TP_ASSERT(pProjectile, "No projectile found.");

    Event.Power = pProjectile->fPower;

    // Projectiles can launch before the World exists (main menu scene).
    if (entt::locator<World>::has_value())
        World::Get().GetRunner().Trigger(Event);

    return result;
}

static TiltedPhoques::Initializer s_projectileHooks(
    []()
    {
        POINTER_SKYRIMSE(TLaunch, s_launch, 44108, 42928);

        RealLaunch = s_launch.Get();

        TP_HOOK(&RealLaunch, HookLaunch);

#ifdef SKYRIMVR
        HookHeldSpellAim();
#endif

        // A null projectile handle read from the stack here crashes the game; the stub re-reads it and returns false.
        // SE 33672 / VR 0x554980 reads it at +0x397 ([rsp+0x58], 0x158 frame); AE 34452 at +0x374 ([rsp+0x50], 0x138 frame).
        // The read is exactly the five bytes a jump needs, and nothing branches into them.
#ifdef SKYRIMVR
        VersionDbPtr<uint8_t> hookLoc(33672);
        constexpr uint32_t cHandleRead = 0x397;
        constexpr uint32_t cHandleSlot = 0x58;
        constexpr uint32_t cFrameSize = 0x158;
#else
        VersionDbPtr<uint8_t> hookLoc(34452);
        constexpr uint32_t cHandleRead = 0x374;
        constexpr uint32_t cHandleSlot = 0x50;
        constexpr uint32_t cFrameSize = 0x138;
#endif

        // mov rbx, [rsp+slot], which the stub replicates.
        uint8_t* pHandleRead = PatchCheck::At(hookLoc.Get(), cHandleRead);
        if (!PatchCheck::Is(pHandleRead, {0x48, 0x8B, 0x5C, 0x24, static_cast<uint8_t>(cHandleSlot)}))
        {
            spdlog::error("Projectile: {} is not the handle read ({}), leaving it alone; a null projectile handle crashes the game",
                          fmt::ptr(pHandleRead), PatchCheck::Bytes(pHandleRead, 5));
            return;
        }

        struct C : TiltedPhoques::CodeGenerator
        {
            C(uint8_t* apLoc, uint32_t aHandleRead, uint32_t aHandleSlot, uint32_t aFrameSize)
            {
                // replicate
                mov(rbx, ptr[rsp + aHandleSlot]);

                // nullptr check
                cmp(rbx, 0);
                jz("exit");
                // jump back
                jmp_S(apLoc + aHandleRead + 5);

                L("exit");
                // return false; scratch space from the registers
                mov(al, 0);
                add(rsp, aFrameSize);
                pop(r15);
                pop(r14);
                pop(r13);
                pop(r12);
                pop(rdi);
                pop(rsi);
                pop(rbx);
                pop(rbp);
                ret();
            }
        } gen(hookLoc.Get(), cHandleRead, cHandleSlot, cFrameSize);
        TiltedPhoques::Jump(hookLoc.Get() + cHandleRead, gen.getCode());
    });
