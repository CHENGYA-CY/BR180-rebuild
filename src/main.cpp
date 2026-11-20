#include "PCH.h"
#include "Config.h"
#include "Hooks.h"

#include "Version.h"

// Address Library format 5 (Skyrim 1.7.99+) requires plugins to explicitly declare
// compatibility. CommonLibSSE-NG >= 6.4.0 sets this flag automatically via
// PluginVersionData; this declaration matches the upstream SkyFlip pattern that is
// known to load on 1.7.104.
SKSE_PLUGIN_VERSION = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ Version::MAJOR, Version::MINOR, Version::PATCH });
    v.PluginName("BR180 - Map Flip for Beyond Reach");
    v.AuthorName("vagonumero12 (original), rebuilt for 1.7.104");
    v.UsesAddressLibrary();
    v.UsesUpdatedStructs();
    v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
    v.MinimumRequiredXSEVersion(REL::Version{ 2, 3, 0 });
    return v;
}();

static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
        Config::GetSingleton().Load();
        Config::GetSingleton().Log();
    }
    if (a_msg->type == SKSE::MessagingInterface::kPostPostLoad) {
        BR180::InstallHooks();
    }
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, { .log = true, .logName = "BR180", .trampoline = true, .trampolineSize = 256 });

    SKSE::log::info("BR180 v{}.{}.{}-0", Version::MAJOR, Version::MINOR, Version::PATCH);
    SKSE::log::info("Runtime: {:08x}", a_skse->RuntimeVersion().pack());
    SKSE::log::info("SKSE: {:08x}", a_skse->SKSEVersion());

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener("BR180", OnMessage)) {
        SKSE::log::error("Failed to register messaging listener");
        return false;
    }

    return true;
}
