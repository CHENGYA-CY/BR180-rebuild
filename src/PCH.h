#pragma once

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <SKSE/Impl/PCH.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>

namespace stl = SKSE::stl;

using namespace std::literals;

// thunk_call helper (same pattern as SkyFlip) — redirects a call site to T::thunk
// and stores the original function pointer in T::func.
namespace stl
{
    template <class T>
    void write_thunk_call(std::uintptr_t a_src)
    {
        auto& trampoline = REL::GetTrampoline();
        T::func = trampoline.write_call<5>(a_src, T::thunk);
    }
}

inline void InitializeLog()
{
    auto path = SKSE::log::log_directory();
    if (!path) {
        stl::report_and_fail("Failed to find standard logging directory"sv);
    }
    *path /= "BR180.log"sv;
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    auto log = std::make_shared<spdlog::logger>("global_log"sv, std::move(sink));
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S] [%-5l] %v");
}
