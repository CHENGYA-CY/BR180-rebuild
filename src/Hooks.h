#pragma once

// NOTE: This is a REBUILD DRAFT. The 7 hook sites were recovered by static analysis
// (see 逆向发现.md) but the callback ABIs still require in-game dynamic verification.
// Expect iteration after the first build.

namespace BR180
{
    // Beyond Reach worldspace EditorIDs (same list as the companion Sky Flip plugin).
    inline constexpr std::array brWorldspaces{
        "arnimaDUPLICATE003"sv,
        "Arnimatown"sv,
        "arnimaEvermore"sv,
        "ArnimaJehanna"sv,
        "ArnimaJehannaSlums"sv
    };

    inline constexpr char lowerAscii(char a_c) noexcept
    {
        return (a_c >= 'A' && a_c <= 'Z') ? static_cast<char>(a_c - 'A' + 'a') : a_c;
    }

    inline bool startsWithCI(std::string_view a_str, std::string_view a_prefix) noexcept
    {
        if (a_str.size() < a_prefix.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_prefix.size(); ++i) {
            if (lowerAscii(a_str[i]) != lowerAscii(a_prefix[i])) {
                return false;
            }
        }
        return true;
    }

    // Beyond Reach adds every worldspace under the "arnima" editor-ID prefix. Prefix
    // matching (case-insensitive) is far more robust than an exact 5-entry list: the
    // mod ships many more worldspaces (Evermore, Jehanna, dungeons, sub-areas) that
    // also need the flip. Falls back to the explicit list for safety.
    inline bool isBRWorldspace(const RE::TESWorldSpace* a_ws)
    {
        if (!a_ws) {
            return false;
        }
        const char* id = a_ws->GetFormEditorID();
        if (!id || !*id) {
            return false;
        }
        const std::string_view sv{ id };
        if (std::ranges::find(brWorldspaces, sv) != brWorldspaces.end()) {
            return true;
        }
        return startsWithCI(sv, "arnima"sv);
    }

    // The 7 hook sites recovered from BR180.dll, re-located in 1.7.104.
    // Names match 逆向发现.md §4 / §9 (confirmed via string XREF).
    struct HookSites
    {
        static std::uintptr_t updateMapTransition;   // hook1: UpdateObjectUpwardsMapTransition (0x14055949c)
        static std::uintptr_t worldMapUpdate;        // hook2: UpdateObjectUpwardsMapUpdate    (0x140997ee9)
        static std::uintptr_t transitionOnEnter;     // hook3: TransitionOnEnterState          (0x1409987c0) rotation core
        static std::uintptr_t worldMapDataOverride;  // hook4: OverrideWorldMapData            (0x14030e0c0)
        static std::uintptr_t addInputTranslation;   // hook5: MapCameraAddInputTranslation    (0x140558d80)
        static std::uintptr_t addInputRotation;      // hook6: MapCameraAddInputRotation       (0x140558d60)
        static std::uintptr_t setCompassAngle;       // hook7: SetCompassAngle                 (0x140938873)
    };

    void InstallHooks();
    void OnMessage(SKSE::MessagingInterface::Message* a_msg);
}
