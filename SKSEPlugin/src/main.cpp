#include "PCH.h"
#include "DialogueManager.h"
#include "GameUtil.h"
#include "Lifecycle.h"
#include "Papyrus.h"
#include "PlayerMoveSpeed.h"
#include "Serialization.h"
#include "Version.h"

SKSEPluginInfo(
    .Version = { ITS_VERSION_MAJOR, ITS_VERSION_MINOR, ITS_VERSION_PATCH, 0 },
    .Name = "InTheirShoes",
    .Author = "ParanoidKing",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    if (!a_msg) return;

    if (a_msg->type == SKSE::MessagingInterface::kInputLoaded) {
        InTheirShoes::Lifecycle::OnInputLoaded();
    } else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        InTheirShoes::Lifecycle::OnDataLoaded();
    } else if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame) {
        InTheirShoes::Lifecycle::OnPostLoadGame();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    // Plugin load runs on the game's main thread; this is the only place g_mainThreadId is set
    InTheirShoes::g_mainThreadId.store(REX::W32::GetCurrentThreadId(), std::memory_order_relaxed);

    // Init sets up the log (InTheirShoes.log, file and debugger output) and the hooks' trampoline. A logger made
    // before it was replaced, emptying the file.
    SKSE::Init(a_skse, {
        .logLevel = spdlog::level::info,
        .logPattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v",
        .trampoline = true,
        .trampolineSize = InTheirShoes::DialogueManager::kTrampolineSize + InTheirShoes::PlayerMoveSpeed::kTrampolineSize,
    });

    auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    SKSE::log::info("{} v{} loading...", plugin->GetName(), plugin->GetVersion().string());

    // Register Papyrus Native functions
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (papyrus) {
        papyrus->Register(InTheirShoes::Papyrus::Register);
    } else {
        SKSE::log::error("Failed to obtain SKSE Papyrus Interface!");
        return false;
    }

    // Register SKSE Messaging Listener
    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(MessageHandler);
    }

    // Register Co-Save Serialization
    InTheirShoes::Serialization::Register();

    SKSE::log::info("{} loaded successfully.", plugin->GetName());
    return true;
}
