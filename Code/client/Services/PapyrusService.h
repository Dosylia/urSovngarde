#pragma once

#include <Misc/GameVM.h>

#include <atomic>
#include <shared_mutex>
#include <type_traits>

struct TESForm;
struct TESObjectREFR;
struct PapyrusFunctionRegisterEvent;

/**
 * @brief Handles registering and executing Papyrus functions.
 */
struct PapyrusService
{
    PapyrusService(entt::dispatcher& aDispatcher) noexcept;
    ~PapyrusService() noexcept = default;

    TP_NOCOPYMOVE(PapyrusService);

    const void* Get(const String& acNamespace, const String& acFunction) const noexcept;
    size_t FunctionCount() const noexcept;

    void HandlePapyrusFunctionEvent(const PapyrusFunctionRegisterEvent&) noexcept;

private:
    // Written on our update as the game registers its natives, read from any thread that calls one.
    mutable std::shared_mutex m_lock;
    Map<String, void*> m_functions;

    entt::scoped_connection m_papyrusFunctionRegisterConnection;
};

//! A Papyrus native found by name among the functions the game registered, looked up until it is found.
//! The game registers its natives as it starts, and a function asked for before it is recorded, or never registered on some setups, was null.
//! A missing function is never called: its call does nothing and returns a default value, and the first miss is logged.
class PapyrusLookup
{
public:
    PapyrusLookup(const char* acpScope, const char* acpName) noexcept
        : m_pScope(acpScope)
        , m_pName(acpName)
    {
    }

    //! The function, or null while it is not recorded.
    const void* Resolve() const noexcept;

private:
    const char* m_pScope;
    const char* m_pName;
    mutable std::atomic<const void*> m_pFunction{nullptr};
    mutable std::atomic_flag m_reported = ATOMIC_FLAG_INIT;
};

//! What a call to a missing function returns.
template <class Return> Return MissingPapyrusResult() noexcept
{
    if constexpr (!std::is_void_v<Return>)
        return Return{};
}

template <class Return, class Type, class... Args> struct PapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const Type*, Args...);

    PapyrusFunction(const char* acpScope, const char* acpName) noexcept
        : m_lookup(acpScope, acpName)
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        const auto pFunction = reinterpret_cast<TFunction>(m_lookup.Resolve());
        if (!pFunction)
            return MissingPapyrusResult<Return>();
        return pFunction(GameVM::Get()->virtualMachine, 0, apThis, std::forward<Args>(args)...);
    }

private:
    PapyrusLookup m_lookup;
};

template <class Return, class... Args> struct GlobalPapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, Args...);

    GlobalPapyrusFunction(const char* acpScope, const char* acpName) noexcept
        : m_lookup(acpScope, acpName)
    {
    }

    Return operator()(Args... args) const noexcept
    {
        const auto pFunction = reinterpret_cast<TFunction>(m_lookup.Resolve());
        if (!pFunction)
            return MissingPapyrusResult<Return>();
        return pFunction(GameVM::Get()->virtualMachine, std::forward<Args>(args)...);
    }

private:
    PapyrusLookup m_lookup;
};

struct RefrOrInventoryObj
{
    const TESObjectREFR* pRefr;
    TESForm* pInventoryForm;
    uint16_t itemCount;
};

template <class Return, class Type, class... Args> struct LatentPapyrusFunction
{
    using TFunction = Return(__fastcall*)(BSScript::IVirtualMachine*, uint32_t, const RefrOrInventoryObj&, Args...);

    LatentPapyrusFunction(const char* acpScope, const char* acpName) noexcept
        : m_lookup(acpScope, acpName)
    {
    }

    Return operator()(const Type* apThis, Args... args) const noexcept
    {
        const auto pFunction = reinterpret_cast<TFunction>(m_lookup.Resolve());
        if (!pFunction)
            return MissingPapyrusResult<Return>();

        RefrOrInventoryObj self{apThis, nullptr, 0};

        return pFunction(GameVM::Get()->virtualMachine, 0, self, std::forward<Args>(args)...);
    }

private:
    PapyrusLookup m_lookup;
};

#define PAPYRUS_FUNCTION(returnType, scope, name, ...) static PapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope, #name);
#define GLOBAL_PAPYRUS_FUNCTION(returnType, scope, name, ...) static GlobalPapyrusFunction<returnType, __VA_ARGS__> s_p##name(#scope, #name);
#define LATENT_PAPYRUS_FUNCTION(returnType, scope, name, ...) static LatentPapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name(#scope, #name);
