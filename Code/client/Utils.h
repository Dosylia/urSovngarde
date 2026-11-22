#pragma once

#include <optional>
#include "VersionDb.h"
#include "World.h"

#ifndef SKYRIMVR
#define POINTER_SKYRIMSE(className, variableName, sseId, vrId) \
    static VersionDbPtr<className> variableName(sseId)
#else
#define POINTER_SKYRIMSE(className, variableName, sseId, vrId) \
    static VersionDbPtr<className> variableName(vrId)
#endif
#define POINTER_SKYRIMSE_LEGACY(className, variableName, ...) static AutoPtr<decltype()> variableName(__VA_ARGS__)

// TODO: should this be debug only? I removed the check since debug is broken, can only use releasedbg
#define TP_ASSERT(Expr, Msg, ...)                                    \
    if (!(Expr))                                                     \
    {                                                                \
        Utils::Assert(#Expr, fmt::format(Msg, __VA_ARGS__).c_str()); \
    }

struct TESForm;
struct Actor;
struct NiPoint3;

namespace Utils
{

struct ActorOwnershipToken
{
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
};

static void Assert(const char* apExpression, const char* apMessage)
{
    spdlog::critical("Assertion failed ({}): {}", apExpression, apMessage);

    if (IsDebuggerPresent())
        __debugbreak();
}

std::optional<uint32_t> GetServerId(entt::entity aEntity) noexcept;
std::optional<entt::entity> FindEntityByServerId(uint32_t aServerId) noexcept;

/**
 * Find the first entity of a view whose component of one kind has an Id.
 * Which id that is depends on the component: the server's id for RemoteComponent and LocalComponent, the game's form id for FormIdComponent.
 *
 * @param acView The view to search; it must hold the component.
 * @param aId The id to look for.
 * @return The entity, or nothing when no entity of the view has that id.
 */
template <class TComponent, class TView> std::optional<entt::entity> FindById(const TView& acView, const uint32_t aId) noexcept
{
    for (const auto cEntity : acView)
        if (acView.template get<TComponent>(cEntity).Id == aId)
            return cEntity;
    return std::nullopt;
}

/**
 * Find the first entity of a view, which holds FormIdComponent, with a form id.
 *
 * @param acView The view to search.
 * @param aFormId The game's form id.
 * @return The entity, or nothing when no entity of the view has that form id.
 */
template <class View> std::optional<entt::entity> FindByFormId(const View& acView, const uint32_t aFormId) noexcept
{
    return FindById<FormIdComponent>(acView, aFormId);
}

/**
 * Find the server's id of a game form, through the entity of a view that has its form id.
 * For a caller that treats both misses alike; one that tells them apart uses FindByFormId and GetServerId.
 *
 * @param acView The view to search; it must hold FormIdComponent.
 * @param aFormId The game's form id.
 * @return The server id; nothing when no entity of the view has the form id, or when that entity has no server id yet.
 */
template <class TView> std::optional<uint32_t> FindServerIdByFormId(const TView& acView, const uint32_t aFormId) noexcept
{
    const auto cEntity = FindByFormId(acView, aFormId);
    return cEntity ? GetServerId(*cEntity) : std::nullopt;
}
std::optional<ActorOwnershipToken> GetLocalOwnershipToken(uint32_t aFormId) noexcept;
std::optional<ActorOwnershipToken> GetRemoteOwnershipToken(uint32_t aFormId) noexcept;
//! The owner's token when an item drops out of a dead body another player's game runs, within this player's reach.
//! That is a hand taking it (HIGGS uses the game's drop), and the owner's body has to lose it too.
std::optional<ActorOwnershipToken> GetBodyTakeToken(const Actor* apBody, const NiPoint3* apDropAt) noexcept;

template <class T> T* GetByServerId(const uint32_t acServerId) noexcept
{
    if (const auto entity = FindEntityByServerId(acServerId))
    {
        if (const auto* pFormIdComponent = World::Get().try_get<FormIdComponent>(*entity))
        {
            TESForm* pForm = TESForm::GetById(pFormIdComponent->Id);

            if (pForm)
                return Cast<T>(pForm);
        }
    }

    spdlog::debug("{}: form not found for server id {:X}", __FUNCTION__, acServerId);
    return nullptr;
}

void ShowHudMessage(const TiltedPhoques::String& acMessage);

/**
 * Show one of the mod's lines on the HUD, with the mod's name in front, as "urSovngarde: ...".
 *
 * @param acText The line, without the mod's name.
 */
void ShowModHudMessage(const TiltedPhoques::String& acText);
} // namespace Utils

namespace TiltedPhoques
{
template <class TFunc, class TThis, class... TArgs> constexpr decltype(auto) ThisCall(TFunc* aFunction, VersionDbPtr<TThis>& aThis, TArgs&&... args) noexcept
{
    return ThisCall(aFunction, aThis.Get(), args...);
}

template <class TFunc, class TThis, class... TArgs> constexpr decltype(auto) ThisCall(VersionDbPtr<TFunc>& aFunction, VersionDbPtr<TThis>& aThis, TArgs&&... args) noexcept
{
    return ThisCall(aFunction.Get(), aThis.Get(), args...);
}

template <class TFunc, class TThis, class... TArgs> constexpr decltype(auto) ThisCall(VersionDbPtr<TFunc>& aFunction, TThis* apThis, TArgs&&... args) noexcept
{
    return ThisCall(aFunction.Get(), apThis, args...);
}
} // namespace TiltedPhoques
