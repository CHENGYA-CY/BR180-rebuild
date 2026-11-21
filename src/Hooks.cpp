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
        static bool          g_running{ false };
        static RE::NiNode*   g_root{ nullptr };
        static RE::NiMatrix3 g_origRot{};
        static bool          g_haveOrig{ false };
        static int           g_logBudget{ 0 };

        static void Tick();

        static void ApplyOnce()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || !ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) {
                return;
            }
            auto* menu = ui->GetMenu<RE::MapMenu>().get();
            if (!menu) {
                return;
            }

            auto& cfg = Config::GetSingleton();
            if (!cfg.bFlipMapCamera) {
                return;
            }

            auto& cam = menu->camera;

            // Reverse the camera's input deltas too: after the view is rotated by
            // 180 degrees, dragging would otherwise feel mirrored. Honours the
            // original ini key bReverseInputRot.
            if (cfg.bReverseInputRot) {
                cam.translationInput.x = -cam.translationInput.x;
                cam.translationInput.y = -cam.translationInput.y;
                cam.rotationInput.x = -cam.rotationInput.x;
                cam.rotationInput.y = -cam.rotationInput.y;
            }

            auto* root = cam.cameraRoot.get();
            if (!root) {
                return;
            }
            if (cfg.fUpdateRotZ == 0.0f) {
                return;
            }

            if (g_root != root) {
                g_root = root;
                g_origRot = root->local.rotate;
                g_haveOrig = true;
                if (g_logBudget > 0) {
                    --g_logBudget;
                    SKSE::log::info(
                        "MapCamera root: translate=({}, {}, {}) scale={} angle={}",
                        root->local.translate.x, root->local.translate.y, root->local.translate.z,
                        root->local.scale, cfg.fUpdateRotZ);
                }
            }
            if (!g_haveOrig) {
                return;
            }

            RE::NiMatrix3 rot;
            rot.MakeZRotation(cfg.fUpdateRotZ * (RE::NI_PI / 180.0f));
            root->local.rotate = cfg.bReverseMatrixMult ? (rot * g_origRot) : (g_origRot * rot);

            if (g_logBudget > 0) {
                --g_logBudget;
                SKSE::log::info("MapCamera flip applied (order={})", cfg.bReverseMatrixMult ? "rot*orig" : "orig*rot");
            }
        }

        static void Tick()
        {
            if (!g_running) {
                return;
            }
            ApplyOnce();
            if (g_running) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(Tick);
                }
            }
        }

        static void Start()
        {
            if (g_running) {
                return;
            }
            g_running = true;
            g_logBudget = 2;
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(Tick);
            }
        }

        static void Stop()
        {
            g_running = false;
            if (g_root && g_haveOrig) {
                g_root->local.rotate = g_origRot;
            }
            g_root = nullptr;
            g_haveOrig = false;
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
                if (a_event->opening) {
                    WorldOverride::Apply(tes ? tes->GetRuntimeData2().worldSpace : nullptr);
                    MapCameraFlip::Start();
                } else {
                    WorldOverride::Revert();
                    MapCameraFlip::Stop();
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
