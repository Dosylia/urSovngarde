#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <TiltedCore/Platform.hpp>

#include <windows.h>
#include <intrin.h>
#include <cstdint>

// TiltedCore
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Filesystem.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Outcome.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <TiltedCore/Math.hpp>
#include <TiltedCore/TaskQueue.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Initializer.hpp>
#include <TiltedCore/Serialization.hpp>

// TiltedReverse
#include <AutoPtr.hpp>
#include <App.hpp>
#include <FunctionHook.hpp>
#include <Entry.hpp>
#include <Debug.hpp>
#include <ThisCall.hpp>

extern void* RipAllocateN(size_t blockLength);
#define REVERSE_ALLOC_STUB(x) RipAllocateN(x)
#include <JitAssembly.hpp>

#define SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <any>
#include <mutex>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>

// A hook goes in only when its game address resolved.
// TiltedReverse's FunctionHookManager::Add passes a null target to MinHook, which then crashes the game at start.
// An address id missing from the VR table gives a null target, so such a hook is logged and skipped instead.
// The check lives here because the TiltedReverse submodule follows upstream.
template <class T, class U> void AddHookIfResolved(T** appSystemFunction, U* apHookFunction, const bool aDelayed, const char* acpName) noexcept
{
    if (*appSystemFunction == nullptr)
    {
        spdlog::error("Hook {} not installed: its game address did not resolve", acpName);
        return;
    }

    TiltedPhoques::FunctionHookManager::GetInstance().Add(appSystemFunction, apHookFunction, aDelayed);
}

#undef TP_HOOK
#undef TP_HOOK_IMMEDIATE
#define TP_HOOK(systemFunction, hookFunction) AddHookIfResolved(systemFunction, hookFunction, true, #hookFunction)
#define TP_HOOK_IMMEDIATE(systemFunction, hookFunction) AddHookIfResolved(systemFunction, hookFunction, false, #hookFunction)

#include <BuildInfo.h>
#include <Games/Primitives.h>

using TiltedPhoques::Allocator;
using TiltedPhoques::App;
using TiltedPhoques::AutoPtr;
using TiltedPhoques::Buffer;
using TiltedPhoques::List;
using TiltedPhoques::Map;
using TiltedPhoques::Outcome;
using TiltedPhoques::ScopedAllocator;
using TiltedPhoques::ScratchAllocator;
using TiltedPhoques::Set;
using TiltedPhoques::SortedMap;
using TiltedPhoques::StackAllocator;
using TiltedPhoques::String;
using TiltedPhoques::ThisCall;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;

using namespace std::chrono_literals;

#include "Components.h"

#include <Utils.h>
#include <RTTI.h>
