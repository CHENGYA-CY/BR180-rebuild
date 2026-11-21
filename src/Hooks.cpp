#include "PCH.h"
#include "Config.h"
#include "Hooks.h"
#include "Signature.h"

namespace BR180
{
    // ---------------------------------------------------------------------------
    // Hook sites, resolved at runtime by signature scan against SkyrimSE 1.7.104.
    // All patterns below were verified to match exactly once in the 1.7.104 .text
    // section (see 逆向发现.md §9). Base VAs are ImageBase 0x140000000-relative.
    // ---------------------------------------------------------------------------
    std::uintptr_t HookSites::updateMapTransition = 0;
    std::uintptr_t HookSites::worldMapUpdate = 0;
    std::uintptr_t HookSites::transitionOnEnter = 0;
    std::uintptr_t HookSites::worldMapDataOverride = 0;
    std::uintptr_t HookSites::addInputTranslation = 0;
    std::uintptr_t HookSites::addInputRotation = 0;
    std::uintptr_t HookSites::setCompassAngle = 0;

    namespace Patterns
    {
        // hook5: addss xmm1,[rcx+0x10]; addss xmm2,[rcx+0x14]; addss xmm3,[rcx+0x18]  -> 0x140558d80
        constexpr auto addInputTranslation = "F3 0F 58 49 10 F3 0F 58 51 14 F3 0F 58 59 18"sv;
        // hook6: movss xmm0,[rcx+0xc]; addss xmm2,[rcx+8]; subss xmm0,xmm1             -> 0x140558d60
        constexpr auto addInputRotation = "F3 0F 10 41 0C F3 0F 58 51 08 F3 0F 5C C1"sv;
        // hook1: call + addss [rbx+0x48]...                                             -> 0x14055949c
        constexpr auto updateMapTransition = "E8 8F 74 98 00 F3 44 0F 58 43 48 F3 0F 58 73 44"sv;
        // hook2: call + nop + mov rcx,[rbp+0x77]                                        -> 0x140997ee9
        constexpr auto worldMapUpdate = "E8 42 8A 54 00 90 48 8B 4D 77 48 85 C9 74 16 B8"sv;
        // hook3: push rbx; sub rsp,0x20; mov rbx,rcx; call                              -> 0x1409987c0
        constexpr auto transitionOnEnter = "40 53 48 83 EC 20 48 8B D9 E8 A2 0D BC FF C6 43"sv;
        // hook4: 24-byte (20B yields 7 hits; 24B is unique)                             -> 0x14030e0c0
        constexpr auto worldMapDataOverride = "66 90 48 8B 81 58 01 00 00 48 85 C0 74 20 F6 81 A2 00 00 00 04 74 17 48"sv;
        // hook7: call + nop + mov r9,rdi                                                -> 0x140938873
        constexpr auto setCompassAngle = "E8 E8 D8 3B 00 90 4C 8B CF BA 18 00 00 00 44 8D"sv;
    }

    // ---------------------------------------------------------------------------
    // OverrideWorldMapData: the original plugin changes the BR worldspace's map-data
    // fields while the map is open. NG's TESWorldSpace exposes them directly, so no
    // low-level hook is required for these three ini values.
    // ---------------------------------------------------------------------------
    namespace WorldOverride
    {
        struct Saved
        {
            RE::TESWorldSpace* ws{ nullptr };
            std::int16_t       nwCellY{ 0 };
            std::int16_t       seCellX{ 0 };
            float              initialPitch{ 0.0f };
            float              northRotation{ 0.0f };
            bool               applied{ false };
        };

        static Saved             saved;
        static RE::TESWorldSpace* lastLogged{ nullptr };

        static float NormalizeDeg(float a_deg) noexcept
        {
            while (a_deg > 360.0f) {
                a_deg -= 360.0f;
            }
            while (a_deg < 0.0f) {
                a_deg += 360.0f;
            }
            return a_deg;
        }

        // Dump the raw map data once per worldspace. This runs for EVERY worldspace
        // the player opens the map in, so a mis-detected Beyond Reach name is still
        // visible in the log (the key diagnostic when the flip appears to do nothing).
        static void LogOnce(RE::TESWorldSpace* a_ws)
        {
            if (lastLogged == a_ws) {
                return;
            }
            lastLogged = a_ws;
            const char* id = a_ws->GetFormEditorID();
            const char* name = id ? id : "<null>";
            SKSE::log::info("Worldspace '{}': nw=({}, {}) se=({}, {}) br={}",
                name,
                a_ws->worldMapData.nwCellX, a_ws->worldMapData.nwCellY,
                a_ws->worldMapData.seCellX, a_ws->worldMapData.seCellY,
                isBRWorldspace(a_ws));
            SKSE::log::info("Worldspace '{}': usable={}x{} pitch={} minH={} maxH={} northRot={}",
                name,
                a_ws->worldMapData.usableWidth, a_ws->worldMapData.usableHeight,
                a_ws->worldMapData.cameraData.initialPitch,
                a_ws->worldMapData.cameraData.minHeight,
                a_ws->worldMapData.cameraData.maxHeight,
                a_ws->northRotation);
        }

        static void Apply(RE::TESWorldSpace* a_ws)
        {
            if (!a_ws) {
                return;
            }

            LogOnce(a_ws);

            if (!isBRWorldspace(a_ws)) {
                return;
            }

            auto& cfg = Config::GetSingleton();

            if (!saved.applied || saved.ws != a_ws) {
                saved.ws = a_ws;
                saved.nwCellY = a_ws->worldMapData.nwCellY;
                saved.seCellX = a_ws->worldMapData.seCellX;
                saved.initialPitch = a_ws->worldMapData.cameraData.initialPitch;
                saved.northRotation = a_ws->northRotation;
            }

            if (cfg.iExtendCellNW_Y > 0) {
                a_ws->worldMapData.nwCellY = static_cast<std::int16_t>(saved.nwCellY + cfg.iExtendCellNW_Y);
            }
            if (cfg.iExtendCellSE_X > 0) {
                a_ws->worldMapData.seCellX = static_cast<std::int16_t>(saved.seCellX + cfg.iExtendCellSE_X);
            }
            if (cfg.fInitialPitch >= 0.0f) {
                // The game stores initialPitch in DEGREES (ini uses 310, wrapped to 310).
                a_ws->worldMapData.cameraData.initialPitch = NormalizeDeg(cfg.fInitialPitch);
            }
            if (cfg.bFlipNorthRotation) {
                a_ws->northRotation = NormalizeDeg(saved.northRotation + 180.0f);
            }
            saved.applied = true;

            SKSE::log::info("Applied map override for '{}' (pitch {}->{}, northRot {}->{}, nwY {}->{}, seX {}->{})",
                a_ws->GetFormEditorID() ? a_ws->GetFormEditorID() : "<null>",
                saved.initialPitch, a_ws->worldMapData.cameraData.initialPitch,
                saved.northRotation, a_ws->northRotation,
                saved.nwCellY, a_ws->worldMapData.nwCellY,
                saved.seCellX, a_ws->worldMapData.seCellX);
        }

        static void Revert()
        {
            if (saved.applied && saved.ws) {
                saved.ws->worldMapData.nwCellY = saved.nwCellY;
                saved.ws->worldMapData.seCellX = saved.seCellX;
                saved.ws->worldMapData.cameraData.initialPitch = saved.initialPitch;
                saved.ws->northRotation = saved.northRotation;
                saved.applied = false;
                saved.ws = nullptr;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Input-sign hooks (hook5 / hook6). The original flips the sign bit of the XMM
    // input vector via XOR with 0x80000000 when bReverseInputRot is set. We call the
    // original first, then negate the accumulated input fields.
    //
    // !! ABI WARNING: thunk signature is inferred, not dynamically verified. !!
    // ---------------------------------------------------------------------------
    struct AddInputTranslationHook
    {
        static void Thunk(RE::TESCamera* a_cam)
        {
            func(a_cam);
            auto& cfg = Config::GetSingleton();
            if (cfg.bReverseInputRot) {
                a_cam->translationInput.x = -a_cam->translationInput.x;
                a_cam->translationInput.y = -a_cam->translationInput.y;
            }
        }
        static inline REL::Relocation<decltype(Thunk)> func;
    };

    struct AddInputRotationHook
    {
        static void Thunk(RE::TESCamera* a_cam)
        {
            func(a_cam);
            auto& cfg = Config::GetSingleton();
            if (cfg.bReverseInputRot) {
                a_cam->rotationInput.x = -a_cam->rotationInput.x;
                a_cam->rotationInput.y = -a_cam->rotationInput.y;
            }
        }
        static inline REL::Relocation<decltype(Thunk)> func;
    };

    // ---------------------------------------------------------------------------
    // Map camera flip.
    //
    // The original plugin's rotation lives inside the map camera's internal math
    // (hook1/2/3), which cannot be reproduced byte-for-byte without live debugging.
    // Instead we rotate the map's render root (MapMenu::camera.cameraRoot, a NiNode)
    // by the configured Z angle, and re-apply it every frame while the map is open,
    // because the game rewrites the node transform when it sets up the camera.
    //
    // Rotation is applied around the node's own origin, so no translation correction
    // is needed. Re-applying is idempotent: the target is always rot(angle) * original.
    // ---------------------------------------------------------------------------
    namespace MapCameraFlip
    {
        static bool          g_active{ false };
        static RE::NiNode*   g_root{ nullptr };
        static RE::NiMatrix3 g_origRot{};
        static bool          g_haveOrig{ false };
        static int           g_retriesLeft{ 0 };
        static int           g_logBudget{ 0 };
        static int           g_probeLeft{ 0 };

        static bool LooksLikeValidPtr(const void* a_ptr) noexcept
        {
            const auto addr = reinterpret_cast<std::uintptr_t>(a_ptr);
            return addr >= 0x10000 && addr < 0x7FFFFFFFFFFF;
        }

        static void Tick();

        static void ApplyOnce()
        {
            const bool probe = g_probeLeft > 0;

            auto* ui = RE::UI::GetSingleton();
            if (probe) {
                SKSE::log::info("probe: ui={} mapOpen={}",
                    static_cast<const void*>(ui),
                    ui ? ui->IsMenuOpen(RE::MapMenu::MENU_NAME) : false);
            }
            if (!ui || !ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) {
                if (probe) {
                    --g_probeLeft;
                }
                return;
            }

            auto* menu = ui->GetMenu<RE::MapMenu>().get();
            if (probe) {
                SKSE::log::info("probe: menu={} valid={}",
                    static_cast<const void*>(menu), LooksLikeValidPtr(menu) ? "yes" : "no");
            }
            if (!menu || !LooksLikeValidPtr(menu)) {
                if (probe) {
                    --g_probeLeft;
                }
                return;
            }

            auto& cam = menu->camera;

            if (probe) {
                // Verify the MapMenu::camera / MapCamera offsets actually match this
                // build: both worldSpace pointers must equal the player's worldspace.
                auto* tes = RE::TES::GetSingleton();
                auto* curWs = (tes && tes->GetRuntimeData2().worldSpace) ? tes->GetRuntimeData2().worldSpace : nullptr;
                SKSE::log::info("probe: menu.worldSpace={} camera.worldSpace={} currentWorld={} match={}",
                    static_cast<const void*>(menu->worldSpace),
                    static_cast<const void*>(cam.worldSpace),
                    static_cast<const void*>(curWs),
                    cam.worldSpace == curWs);
                SKSE::log::info("probe: cameraRoot={} valid={} currentState={}",
                    static_cast<const void*>(cam.cameraRoot.get()),
                    LooksLikeValidPtr(cam.cameraRoot.get()) ? "yes" : "no",
                    static_cast<const void*>(cam.currentState.get()));
                SKSE::log::info("probe: unk38=({}, {}) unk40=({}, {}, {}) unk4C={}",
                    cam.unk38.x, cam.unk38.y,
                    cam.unk40.x, cam.unk40.y, cam.unk40.z,
                    cam.unk4C);
            }

            auto& cfg = Config::GetSingleton();
            const int mode = cfg.iFlipMode;
            if (mode <= 0 || mode > 3) {
                if (probe) {
                    SKSE::log::info("probe: iFlipMode={} -> transform disabled", mode);
                    --g_probeLeft;
                }
                return;
            }

            // Reverse the camera's input deltas too: a mirrored / rotated view makes
            // dragging feel inverted. Honours the original ini key bReverseInputRot.
            if (cfg.bReverseInputRot) {
                cam.translationInput.x = -cam.translationInput.x;
                cam.translationInput.y = -cam.translationInput.y;
                cam.rotationInput.x = -cam.rotationInput.x;
                cam.rotationInput.y = -cam.rotationInput.y;
            }

            auto* root = cam.cameraRoot.get();
            if (!root || !LooksLikeValidPtr(root)) {
                if (probe) {
                    SKSE::log::warn("probe: cameraRoot unusable -> cannot rotate");
                    --g_probeLeft;
                }
                return;
            }
            if (g_root != root) {
                g_root = root;
                g_origRot = root->local.rotate;
                g_haveOrig = true;
                if (g_logBudget > 0) {
                    --g_logBudget;
                    SKSE::log::info(
                        "MapCamera root: translate=({}, {}, {}) scale={} mode={}",
                        root->local.translate.x, root->local.translate.y, root->local.translate.z,
                        root->local.scale, mode);
                }
            }
            if (!g_haveOrig) {
                return;
            }

            // mode 1/2 are reflections (determinant -1); mode 3 is a rotation (+1).
            RE::NiMatrix3 xform;  // identity
            switch (mode) {
            case 1:  // mirror left/right
                xform.entry[0][0] = -1.0f;
                break;
            case 2:  // mirror top/bottom
                xform.entry[1][1] = -1.0f;
                break;
            case 3:  // rotate 180 degrees
                xform.MakeZRotation(RE::NI_PI);
                break;
            default:
                return;
            }
            root->local.rotate = cfg.bReverseMatrixMult ? (xform * g_origRot) : (g_origRot * xform);

            if (probe) {
                --g_probeLeft;
            }
            if (g_logBudget > 0) {
                --g_logBudget;
                SKSE::log::info("MapCamera transform applied (mode={} order={})",
                    mode, cfg.bReverseMatrixMult ? "xform*orig" : "orig*xform");
            }
        }

        // Bounded: applies now plus a few following frames, then stops.
        // CRITICAL: this must never re-arm itself unconditionally. SKSE drains newly
        // queued tasks inside the same tick, so an always-AddTask loop freezes the
        // game main thread (v6 bug: map open -> hard hang).
        static void Tick()
        {
            if (!g_active) {
                return;
            }
            ApplyOnce();
            if (g_active && --g_retriesLeft > 0) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(Tick);
                }
            }
        }

        static void Start()
        {
            if (g_active) {
                return;
            }
            auto& cfg = Config::GetSingleton();
            if (cfg.iFlipMode <= 0 || cfg.iFlipMode > 3) {
                return;
            }
            g_active = true;
            g_retriesLeft = 4;  // total 5 applications, then stop
            g_logBudget = 3;
            g_probeLeft = 3;
            SKSE::log::info("MapCameraFlip start (bounded, retries={})", g_retriesLeft);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(Tick);
            }
        }

        static void Stop()
        {
            g_active = false;
            g_retriesLeft = 0;
            // Deliberately do NOT dereference g_root here: the node may already be
            // torn down, and keeping g_origRot prevents the rotation from compounding
            // when the map is reopened on the same node instance.
        }
    }

    // ---------------------------------------------------------------------------
    // Scaleform map mirror.
    //
    // The world map is NOT a 3D scene: Skyrim renders it through MapMenu.swf, so
    // rotating a NiNode (v6..v9) can never change what the player sees -- confirmed
    // by the probe logs, where MapMenu::camera had cameraRoot == null and
    // currentState == null forever.
    //
    // MapMenu.swf (extracted from Skyrim - Interface.bsa) contains a real display
    // object for the map and it does export the _xscale / _yscale properties, so the
    // mirror is done by setting those to -100 (percent) on the map container.
    //
    // The exact instance name is not documented, so every plausible candidate is
    // tried and the results are logged: whichever resolves first wins, and the log
    // tells us the truth even if none of them match.
    // ---------------------------------------------------------------------------
    namespace ScaleformMirror
    {
        static RE::GFxValue g_target{};
        static bool         g_haveTarget{ false };
        static bool         g_fromMapMovie{ false };
        static int          g_probeLeft{ 0 };
        static bool         g_reported{ false };

        static const char* SafeName(RE::GFxValue& a_val)
        {
            RE::GFxValue nm;
            if (a_val.GetMember("_name", &nm) && nm.IsString()) {
                if (const char* s = nm.GetString()) {
                    return s;
                }
            }
            return "";
        }

        // Walk _root's children by depth and log every display object. The world map
        // container is created at run time by ActionScript, so it is not in the SWF's
        // static structure -- but it must be reachable as a child of _root.
        static void Enumerate(RE::GFxMovieView* a_view)
        {
            RE::GFxValue root;
            if (!a_view->GetVariable(&root, "_root")) {
                SKSE::log::warn("scaleform: cannot read _root");
                return;
            }
            SKSE::log::info("scaleform: _root type={}", static_cast<int>(root.GetType()));

            int shown = 0;
            for (int d = 0; d < 256 && shown < 40; ++d) {
                RE::GFxValue depthVal(static_cast<double>(d));
                RE::GFxValue inst;
                if (!root.Invoke("getInstanceAtDepth", &inst, &depthVal, 1)) {
                    continue;
                }
                if (!inst.IsDisplayObject()) {
                    continue;
                }
                const char* nm = SafeName(inst);
                SKSE::log::info("scaleform: _root depth={} name='{}'", d, nm);
                ++shown;

                if (!g_haveTarget && nm && nm[0]) {
                    std::string lower{ nm };
                    for (auto& c : lower) {
                        c = lowerAscii(c);
                    }
                    if (lower.find("map") != std::string::npos) {
                        g_target = inst;
                        g_haveTarget = true;
                        SKSE::log::info("scaleform: picked display object '{}' at depth {}", nm, d);
                    }
                }
            }
            if (!g_haveTarget) {
                SKSE::log::warn("scaleform: no _root child with 'map' in its name");
            }
        }

        static void Apply(RE::MapMenu* a_menu, int a_mode)
        {
            if (!a_menu) {
                return;
            }
            auto* view = a_menu->uiMovie.get();
            if (!view) {
                if (g_probeLeft > 0) {
                    --g_probeLeft;
                    SKSE::log::warn("scaleform: uiMovie is null");
                }
                return;
            }

            if (!g_haveTarget && g_probeLeft > 0) {
                --g_probeLeft;

                // 1) C++ side: MapMenu::mapMovie holds the GFxValue of the map clip.
                auto& mm = a_menu->mapMovie;
                SKSE::log::info("scaleform: MapMenu::mapMovie type={}", static_cast<int>(mm.GetType()));
                if (mm.IsDisplayObject()) {
                    g_target = mm;
                    g_haveTarget = true;
                    g_fromMapMovie = true;
                    SKSE::log::info("scaleform: using MapMenu::mapMovie (display object)");
                }

                // 2) AS2 side. The bytecode shows MapMovie is assigned as a member of
                //    the MapMenu class (_global.Map.MapMenu.MapMovie), and it is the
                //    container that owns MarkerClips (world map markers) as well as
                //    LocalMapFader.MapClip (interior map).
                if (!g_haveTarget) {
                    static constexpr const char* kPaths[] = {
                        "_global.Map.MapMenu.MapMovie",
                        "_global.MapMenu.MapMovie",
                        "_root.MapMovie",
                        "_root.WorldMap.MapMovie",
                    };
                    for (const auto* p : kPaths) {
                        RE::GFxValue v;
                        const bool ok = view->GetVariable(&v, p);
                        SKSE::log::info("scaleform: {} ok={} type={}",
                            p, ok, static_cast<int>(v.GetType()));
                        if (ok && v.IsDisplayObject()) {
                            g_target = v;
                            g_haveTarget = true;
                            SKSE::log::info("scaleform: using {}", p);
                            break;
                        }
                    }
                }

                // 3) Last resort: enumerate _root children (diagnostic value only).
                if (!g_haveTarget) {
                    Enumerate(view);
                }
            }

            if (!g_haveTarget) {
                return;
            }

            const double sx = (a_mode == 1 || a_mode == 3) ? -100.0 : 100.0;
            const double sy = (a_mode == 2 || a_mode == 3) ? -100.0 : 100.0;

            RE::GFxValue::DisplayInfo info;
            info.SetXScale(sx);
            info.SetYScale(sy);
            const bool ok = g_target.SetDisplayInfo(info);

            if (!g_reported) {
                g_reported = true;
                SKSE::log::info("scaleform mirror: fromMapMovie={} xscale={} yscale={} ok={}",
                    g_fromMapMovie, sx, sy, ok);
            }
        }

        static void Reset()
        {
            g_reported = false;
            g_haveTarget = false;
            g_fromMapMovie = false;
            g_target = RE::GFxValue();
        }

        static void OnMenuOpen()
        {
            // Re-resolve every time the map opens: the GFxValue handle must not be
            // reused across menu instances, and the game may rebuild the clip.
            Reset();
            g_probeLeft = 2;
        }
    }

    // -------------------------------------------------------------------------
    // MapMirror — per-frame hook on MapMenu::AdvanceMovie (vtable index 5).
    //
    // Reverse-engineering rationale (why every earlier approach failed):
    //   * MapMenu::RUNTIME_DATA2 contains `bool cameraSetupPending` (0x12B):
    //     the map camera is built LAZILY after the menu opens. SKSE's task queue
    //     drains within the SAME frame, so any probe fired from MenuOpenCloseEvent
    //     sees a null cameraRoot -- exactly what previous builds logged.
    //   * MapMenu overrides IMenu::AdvanceMovie (vtable index 5) and the UI calls
    //     it once per frame while the map is up. That is the only place where the
    //     camera is guaranteed to exist, and where re-applying the transform each
    //     frame defeats any rewrite the game performs.
    //   * Mirroring is done by negating matrix ROWS, i.e. pre-multiplying by
    //     diag(-1,1,1) (left/right) or diag(1,-1,1) (up/down). This avoids relying
    //     on NiMatrix3::operator* and on matrix storage-order assumptions beyond
    //     row-major (Skyrim's NiMatrix3 is row-major).
    // -------------------------------------------------------------------------
    namespace MapMirror
    {
        using AdvanceMovie_t = void (*)(RE::MapMenu*, float, std::uint32_t);

        static AdvanceMovie_t g_orig{ nullptr };
        static bool           g_installed{ false };
        static RE::NiNode*    g_root{ nullptr };
        static RE::NiMatrix3  g_origRot{};      // pristine baseline (game's own matrix)
        static RE::NiMatrix3  g_written{};      // what we last wrote
        static bool           g_haveOrig{ false };
        static bool           g_hasWritten{ false };
        static int            g_waitLog{ 8 };
        static int            g_applyLog{ 3 };

        static bool SameMatrix(const RE::NiMatrix3& a_lhs, const RE::NiMatrix3& a_rhs)
        {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    if (std::fabs(a_lhs.entry[r][c] - a_rhs.entry[r][c]) > 1e-5f) {
                        return false;
                    }
                }
            }
            return true;
        }

        static void ApplyTransform(RE::MapMenu* a_menu, int a_mode)
        {
            auto* root = a_menu->camera.cameraRoot.get();
            if (!root) {
                if (g_waitLog > 0) {
                    --g_waitLog;
                    SKSE::log::info("mirror: camera pending={} root=null (frame {})",
                        a_menu->cameraSetupPending ? 1 : 0, 8 - g_waitLog);
                }
                return;
            }

            // ---- baseline selection -------------------------------------------
            // The camera node is REUSED between map open/close cycles. If we simply
            // re-cached whatever the node currently holds, on the second open we
            // would cache our OWN mirrored matrix as the "original" and mirroring
            // it again would silently undo the effect. So:
            //   * different node        -> adopt its current matrix as baseline
            //   * same node, but matrix != what we wrote
            //                           -> the game overwrote it, adopt that value
            //   * same node, matrix == what we wrote
            //                           -> ours is still in place, keep baseline
            if (g_root != root || !g_haveOrig) {
                g_root = root;
                g_origRot = root->local.rotate;
                g_haveOrig = true;
                g_hasWritten = false;
                if (g_applyLog > 0) {
                    --g_applyLog;
                    SKSE::log::info("mirror: camera root acquired {} translate=({}, {}, {}) mode={}",
                        static_cast<const void*>(root),
                        root->local.translate.x, root->local.translate.y, root->local.translate.z,
                        a_mode);
                    SKSE::log::info("mirror: camera state pending={} state={} ws={}",
                        a_menu->cameraSetupPending ? 1 : 0,
                        static_cast<const void*>(a_menu->camera.currentState.get()),
                        static_cast<const void*>(a_menu->camera.worldSpace));
                    SKSE::log::info("mirror: origRot rows = [{}, {}, {}] [{}, {}, {}] [{}, {}, {}]",
                        g_origRot.entry[0][0], g_origRot.entry[0][1], g_origRot.entry[0][2],
                        g_origRot.entry[1][0], g_origRot.entry[1][1], g_origRot.entry[1][2],
                        g_origRot.entry[2][0], g_origRot.entry[2][1], g_origRot.entry[2][2]);
                }
            } else if (g_hasWritten && !SameMatrix(root->local.rotate, g_written)) {
                // The game re-derived the transform; take its value as the new baseline.
                g_origRot = root->local.rotate;
                if (g_applyLog > 0) {
                    --g_applyLog;
                    SKSE::log::info("mirror: game rewrote the transform, baseline refreshed");
                }
            }
            if (!g_haveOrig) {
                return;
            }

            // Start from the pristine matrix so repeated application never accumulates.
            auto m = g_origRot;
            if (a_mode == 1) {
                // left/right mirror: negate row 0  (pre-multiply by diag(-1,1,1))
                m.entry[0][0] = -g_origRot.entry[0][0];
                m.entry[0][1] = -g_origRot.entry[0][1];
                m.entry[0][2] = -g_origRot.entry[0][2];
            } else if (a_mode == 2) {
                // up/down mirror: negate row 1   (pre-multiply by diag(1,-1,1))
                m.entry[1][0] = -g_origRot.entry[1][0];
                m.entry[1][1] = -g_origRot.entry[1][1];
                m.entry[1][2] = -g_origRot.entry[1][2];
            } else {
                // 180 degrees about Z: negate rows 0 and 1
                m.entry[0][0] = -g_origRot.entry[0][0];
                m.entry[0][1] = -g_origRot.entry[0][1];
                m.entry[0][2] = -g_origRot.entry[0][2];
                m.entry[1][0] = -g_origRot.entry[1][0];
                m.entry[1][1] = -g_origRot.entry[1][1];
                m.entry[1][2] = -g_origRot.entry[1][2];
            }
            root->local.rotate = m;
            g_written = m;
            g_hasWritten = true;

            if (g_applyLog > 0) {
                --g_applyLog;
                SKSE::log::info("mirror: applied mode={} to camera root", a_mode);
            }
        }

        static void Hook_AdvanceMovie(RE::MapMenu* a_menu, float a_interval, std::uint32_t a_time)
        {
            if (g_orig) {
                g_orig(a_menu, a_interval, a_time);
            }
            if (!a_menu) {
                return;
            }
            auto& cfg = Config::GetSingleton();
            if (cfg.iFlipMode <= 0 || cfg.iFlipMode > 3) {
                return;
            }
            const auto tes = RE::TES::GetSingleton();
            auto* ws = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
            if (!isBRWorldspace(ws)) {
                return;
            }
            ApplyTransform(a_menu, cfg.iFlipMode);
        }

        static void Install(RE::MapMenu* a_menu)
        {
            if (g_installed || !a_menu) {
                return;
            }
            // `a_menu` starts with the vptr; dereferencing it yields the vtable
            // address, i.e. a pointer to the array of entry addresses.
            auto* vtbl = *reinterpret_cast<std::uintptr_t**>(a_menu);
            if (!vtbl) {
                SKSE::log::error("mirror: MapMenu has null vtable");
                return;
            }
            const auto slot = reinterpret_cast<std::uintptr_t>(&vtbl[5]);
            const auto cur = vtbl[5];
            const auto hook = reinterpret_cast<std::uintptr_t>(&Hook_AdvanceMovie);
            if (cur == hook) {
                g_installed = true;
                return;
            }
            // Sanity report: on 1.7.104 the expected original is 0x14099bd30
            // (MapMenu::AdvanceMovie). If a different value shows up in the log,
            // another mod hooked it first (we chain correctly) or the vtable layout
            // differs -- either way the log tells us immediately.
            SKSE::log::info("mirror: MapMenu vtable={} vtbl[5]={:x} (expect 14099bd30)",
                static_cast<const void*>(vtbl), cur);
            g_orig = reinterpret_cast<AdvanceMovie_t>(cur);
            REL::safe_write(slot, &hook, sizeof(hook));
            g_installed = true;
            SKSE::log::info("mirror: hooked MapMenu::AdvanceMovie vtbl[5] -> {:x}", hook);
        }

        static void ResetForOpen()
        {
            // Deliberately KEEP g_root / g_origRot / g_written / g_haveOrig across
            // map open-close cycles. The camera node is reused by the game; clearing
            // the cached baseline would make the next open cache our own mirrored
            // matrix as the "original", which silently cancels the effect.
            g_waitLog = 8;
            g_applyLog = 3;
        }
    }

    // Map menu open/close listener drives the worldspace overrides only while the map
    // is open (matches "only for the map menu" semantics of the original ini).
    class MapMenuListener : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MapMenuListener* GetSingleton()
        {
            static MapMenuListener singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!a_event || a_event->menuName.empty()) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (a_event->menuName == RE::MapMenu::MENU_NAME) {
                const auto tes = RE::TES::GetSingleton();
                auto* ui = RE::UI::GetSingleton();
                auto* mapMenu = ui ? ui->GetMenu<RE::MapMenu>().get() : nullptr;
                if (a_event->opening) {
                    WorldOverride::Apply(tes ? tes->GetRuntimeData2().worldSpace : nullptr);
                    MapMirror::ResetForOpen();
                    MapMirror::Install(mapMenu);
                } else {
                    WorldOverride::Revert();
                    MapMirror::ResetForOpen();
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void InstallHooks()
    {
        // Resolve every hook site by signature scan (each pattern verified unique).
        HookSites::updateMapTransition = Scan::Find(Patterns::updateMapTransition);
        HookSites::worldMapUpdate = Scan::Find(Patterns::worldMapUpdate);
        HookSites::transitionOnEnter = Scan::Find(Patterns::transitionOnEnter);
        HookSites::worldMapDataOverride = Scan::Find(Patterns::worldMapDataOverride);
        HookSites::addInputTranslation = Scan::Find(Patterns::addInputTranslation);
        HookSites::addInputRotation = Scan::Find(Patterns::addInputRotation);
        HookSites::setCompassAngle = Scan::Find(Patterns::setCompassAngle);

        SKSE::log::info("Hook sites: trans={:x} rot={:x} wupd={:x} wmd={:x} enter={:x} trans2={:x} compass={:x}",
            HookSites::addInputTranslation, HookSites::addInputRotation,
            HookSites::worldMapUpdate, HookSites::worldMapDataOverride,
            HookSites::transitionOnEnter, HookSites::updateMapTransition,
            HookSites::setCompassAngle);

        // ---------------------------------------------------------------------
        // BASELINE BUILD — no code hooks are installed.
        //
        // hook5/6 land on function-ENTRY instructions (addss / movss), not on
        // call instructions. write_call<5>() would overwrite real code and
        // return a bogus "original function" pointer -> guaranteed CTD.
        // hook1/2/7 land on E8 call sites (patchable), but their thunk ABIs
        // are not yet verified. hook3/4 need entry detours with a trampoline
        // that replays the overwritten prologue bytes.
        //
        // All 7 sites are resolved and logged above. This build therefore loads
        // cleanly on 1.7.104 (format 5) and applies only the struct-level
        // overrides, which are safe. Hooks are enabled one by one in the next
        // iteration after their callback signatures are verified in-game.
        // ---------------------------------------------------------------------
        SKSE::log::info("Baseline build: {} hook sites resolved; code hooks disabled pending ABI verification", 7);

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(MapMenuListener::GetSingleton());
            SKSE::log::info("Registered MapMenu listener");
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame) {
            const auto tes = RE::TES::GetSingleton();
            WorldOverride::Apply(tes ? tes->GetRuntimeData2().worldSpace : nullptr);
        }
    }
}
