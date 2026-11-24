#include "PCH.h"
#include "PlayerMoveSpeed.h"
#include "HookUtil.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>

namespace InTheirShoes::PlayerMoveSpeed {

    namespace {
        // How the engine sets an actor's walk/run while the AI moves it (AE 1.6.1170):
        // - When a procedure starts moving the actor (Travel does it on every path segment), the AI picks walk or run
        //   (AE 39362: the package's preferred speed, else run if the actor's force-run flag is set, else by
        //   distance), then applies it through set movement speed below. The path keeps that speed until the next.
        // - Set movement speed (AE 39363 / SE 38379; no CommonLib wrapper) puts the walk/run bits in the actor's
        //   ActorState1 and refreshes the speed of the path it follows (on its movement controller), so a call
        //   changes the speed mid-path. Its 22 callers are procedures, combat, follow and so on.
        // Writing ActorState1, the force-run flag or the package's preferred speed alone didn't work: the next path
        // segment picked its own speed again. The SE ID was matched to the AE one by function layout in the Address
        // Library, not tested on SE.
        using SetMovementSpeed_t = void(RE::AIProcess* a_process, RE::Actor* a_actor, std::uint32_t a_flags,
            bool a_usePackageSpeed);

        // a_flags is the actor's ActorState1 with the speed it should move at, or -1 for walk.
        // a_usePackageSpeed: the running package's preferred speed, if it has one, wins.
        std::uintptr_t SetMovementSpeedAddress() {
            return REL::Relocation<std::uintptr_t>{ RELOCATION_ID(38379, 39363) }.address();
        }

        // Jog (0x1000) and fast walk (0x2000), the speeds besides walk and run that packages can ask for; CommonLib
        // leaves them unnamed in ActorState1::unk11. The engine clears all four speed bits and keeps the others
        // when it applies a package's preferred speed; so does the hook.
        constexpr std::uint32_t kJogAndFastWalk = 0b110;

        enum class Speed : std::uint8_t {
            kEngine,  // The AI's own choice
            kWalk,
            kRun,
        };
        // Read by the hook on AI threads
        std::atomic<Speed> g_speed{ Speed::kEngine };

        SetMovementSpeed_t* g_original{ nullptr };

        // Runs on AI threads: reads only g_speed
        void Hook_SetMovementSpeed(RE::AIProcess* a_process, RE::Actor* a_actor, std::uint32_t a_flags, bool a_usePackageSpeed) {
            const auto speed = g_speed.load();
            if (speed == Speed::kEngine || !a_actor || !a_actor->IsPlayerRef()) {
                return g_original(a_process, a_actor, a_flags, a_usePackageSpeed);
            }
            RE::ActorState::ActorState1 state{};
            if (a_flags != std::numeric_limits<std::uint32_t>::max()) {
                state = std::bit_cast<RE::ActorState::ActorState1>(a_flags);
            }
            state.walking = speed == Speed::kWalk;
            state.running = speed == Speed::kRun;
            state.unk11 &= ~kJogAndFastWalk;
            g_original(a_process, a_actor, std::bit_cast<std::uint32_t>(state), false);
        }
    }

    void Install() {
        constexpr std::uint8_t kPrologue[] = { 0x48, 0x8B, 0xC4, 0x55, 0x57 };  // mov rax, rsp / push rbp / push rdi
        g_original = HookUtil::HookFunctionStart(SetMovementSpeedAddress(), kPrologue, &Hook_SetMovementSpeed, "set movement speed"sv);
    }

    void Apply(RE::PlayerCharacter* a_player) {
        auto* controls = RE::PlayerControls::GetSingleton();
        if (!controls) return;
        g_speed = controls->data.running ? Speed::kRun : Speed::kWalk;

        // The path under way changes too: set its speed again, through the hook
        auto* state = a_player->AsActorState();
        auto* process = a_player->GetActorRuntimeData().currentProcess;
        if (!g_original || !state || !process) return;
        REL::Relocation<SetMovementSpeed_t> setMovementSpeed{ SetMovementSpeedAddress() };
        setMovementSpeed(process, a_player, std::bit_cast<std::uint32_t>(state->actorState1), false);
    }

    void Release() {
        g_speed = Speed::kEngine;
    }

    void ToggleAlwaysRun(RE::PlayerCharacter* a_player) {
        // The engine's handler for Toggle Always Run skips an AI-driven player, so the setting is changed here
        auto* controls = RE::PlayerControls::GetSingleton();
        if (!controls) return;
        controls->data.running = !controls->data.running;
        Apply(a_player);
        SKSE::log::info("Toggle Always Run while AI-driven: {}", controls->data.running ? "run"sv : "walk"sv);
    }

}
