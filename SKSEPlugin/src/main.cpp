#include "PCH.h"
#include <spdlog/sinks/msvc_sink.h>
#include "Forms.h"
#include "Papyrus.h"
#include "Serialization.h"
#include "IdleAIManager.h"
#include "DialogueManager.h"
#include "ImpersonationManager.h"

SKSEPluginInfo(
    .Version = { 1, 0, 0, 0 },
    .Name = "InTheirShoes",
    .Author = "ParanoidKing",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)

void InitializeLogging() {
    auto path = SKSE::log::log_directory();
    if (!path) {
        return;
    }

    *path /= "InTheirShoes.log"sv;

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

    auto log = std::make_shared<spdlog::logger>("global log"s, sinks.begin(), sinks.end());
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);

    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v"s);
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    if (!a_msg) return;

    if (a_msg->type == SKSE::MessagingInterface::kInputLoaded) {
        InTheirShoes::IdleAIManager::GetSingleton().Initialize();
    } else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        InTheirShoes::Forms::Resolve();
        InTheirShoes::DialogueManager::GetSingleton().Initialize();
        InTheirShoes::ImpersonationManager::GetSingleton().Initialize();
    } else if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame) {
        InTheirShoes::ImpersonationManager::GetSingleton().OnPostLoadGame();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    // Plugin load runs on the game's main thread; this is the only place g_mainThreadId is set
    InTheirShoes::g_mainThreadId.store(REX::W32::GetCurrentThreadId(), std::memory_order_relaxed);
    InitializeLogging();

    auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    SKSE::log::info("{} v{} loading...", plugin->GetName(), plugin->GetVersion().string());

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(512);

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
