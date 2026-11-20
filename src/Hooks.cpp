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
            bool               applied{ false };
        };

        static Saved saved;

        static void Apply(RE::TESWorldSpace* a_ws)
        {
            auto& cfg = Config::GetSingleton();
            if (!a_ws || !isBRWorldspace(a_ws)) {
                return;
            }

            if (!saved.applied || saved.ws != a_ws) {
                saved.ws = a_ws;
                saved.nwCellY = a_ws->worldMapData.nwCellY;
                saved.seCellX = a_ws->worldMapData.seCellX;
                saved.initialPitch = a_ws->worldMapData.cameraData.initialPitch;
            }

            if (cfg.iExtendCellNW_Y > 0) {
                a_ws->worldMapData.nwCellY = static_cast<std::int16_t>(saved.nwCellY + cfg.iExtendCellNW_Y);
            }
            if (cfg.iExtendCellSE_X > 0) {
                a_ws->worldMapData.seCellX = static_cast<std::int16_t>(saved.seCellX + cfg.iExtendCellSE_X);
            }
            if (cfg.fInitialPitch >= 0.0f) {
                // The game stores initialPitch in DEGREES (ini uses 310 with >360 wrap).
                float deg = cfg.fInitialPitch;
                while (deg > 360.0f) {
                    deg -= 360.0f;
                }
                a_ws->worldMapData.cameraData.initialPitch = deg;
            }
            saved.applied = true;

            SKSE::log::info("Applied world map override for {}",
                a_ws->GetFormEditorID() ? a_ws->GetFormEditorID() : "BR worldspace");
        }

        static void Revert()
        {
            if (saved.applied && saved.ws) {
                saved.ws->worldMapData.nwCellY = saved.nwCellY;
                saved.ws->worldMapData.seCellX = saved.seCellX;
                saved.ws->worldMapData.cameraData.initialPitch = saved.initialPitch;
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
                    WorldOverride::Apply(tes && tes->GetRuntimeData2() ? tes->GetRuntimeData2()->worldSpace : nullptr);
                } else {
                    WorldOverride::Revert();
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

        // Input-sign hooks: install only where the signature resolved.
        if (HookSites::addInputTranslation) {
            br_stl::write_thunk_call<AddInputTranslationHook>(HookSites::addInputTranslation);
            SKSE::log::info("Installed AddInputTranslation hook @ {:x}", HookSites::addInputTranslation);
        } else {
            SKSE::log::warn("AddInputTranslation signature not found");
        }

        if (HookSites::addInputRotation) {
            br_stl::write_thunk_call<AddInputRotationHook>(HookSites::addInputRotation);
            SKSE::log::info("Installed AddInputRotation hook @ {:x}", HookSites::addInputRotation);
        } else {
            SKSE::log::warn("AddInputRotation signature not found");
        }

        // NOTE: hook1/2/3/4/7 are located but intentionally NOT installed in this draft.
        // Their callbacks use unverified ABIs and hardcoded object offsets; installing
        // them blindly would crash. They are logged above for the next iteration after
        // in-game dynamic verification.

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
            WorldOverride::Apply(tes && tes->GetRuntimeData2() ? tes->GetRuntimeData2()->worldSpace : nullptr);
        }
    }
}
