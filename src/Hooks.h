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

    inline bool isBRWorldspace(const RE::TESWorldSpace* a_ws)
    {
        return a_ws && std::ranges::find(brWorldspaces, a_ws->GetFormEditorID()) != brWorldspaces.end();
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
