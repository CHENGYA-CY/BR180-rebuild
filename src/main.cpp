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

    // Forward to the plugin's own handler (kPostLoadGame -> apply map overrides).
    BR180::OnMessage(a_msg);
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, { .log = true, .logName = "BR180", .trampoline = true, .trampolineSize = 256 });

    SKSE::log::info("BR180 v{}.{}.{}-0", Version::MAJOR, Version::MINOR, Version::PATCH);
    SKSE::log::info("build tag: v15-scaleform (mapMovie mirror, camera route disproven)");
    SKSE::log::info("Runtime: {:08x}", a_skse->RuntimeVersion().pack());
    SKSE::log::info("SKSE: {:08x}", a_skse->SKSEVersion());

    auto messaging = SKSE::GetMessagingInterface();
    // The first argument MUST be "SKSE": that subscribes to SKSE's own message
    // source (kPostLoad / kPostPostLoad / kPostLoadGame). Passing our own plugin
    // name instead makes SKSE look up a *plugin channel* with that exact name, which
    // does not exist (our registered name is the long PluginName), so it returns
    // false -> SKSEPlugin_Load returns false -> SKSE reports
    // "reported as incompatible during load".
    if (!messaging) {
        SKSE::log::error("Failed to get messaging interface");
        return false;
    }
    if (!messaging->RegisterListener("SKSE", OnMessage)) {
        SKSE::log::error("Failed to register SKSE messaging listener");
        return false;
    }
    SKSE::log::info("Registered SKSE messaging listener");

    return true;
}
