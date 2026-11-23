#pragma once

#include <cstring>
#include <span>

#include <xbyak/xbyak.h>

namespace InTheirShoes::HookUtil {

    // The plugin's trampoline (allocated by SKSE::Init in main.cpp): enough for this many HookFunctionStart calls
    inline constexpr std::size_t kMaxHooks = 4;
    inline constexpr std::size_t kTrampolineSize = 64 * kMaxHooks;

    namespace detail {
        // Runs a function's first instructions, then jumps to the rest of it
        struct OriginalStub : Xbyak::CodeGenerator {
            OriginalStub(const std::uint8_t* a_bytes, std::size_t a_size, std::uintptr_t a_resume) {
                Xbyak::Label resume;
                for (std::size_t i = 0; i < a_size; ++i) {
                    db(a_bytes[i]);
                }
                jmp(ptr[rip + resume]);
                L(resume);
                dq(a_resume);
            }
        };
    }

    // Replaces the start of a_function with a jump to a_hook. Returns what calls the original: a stub that runs its
    // first instructions (a_prologue: position independent, at least 5 bytes), then jumps back to the rest. The
    // prologue is checked first; returns null if the function starts differently (another mod hooked it, or another
    // game version), and a_what names it in the log either way.
    template <class F>
    F* HookFunctionStart(std::uintptr_t a_function, std::span<const std::uint8_t> a_prologue, F* a_hook, std::string_view a_what) {
        if (a_prologue.size() < 5 || std::memcmp(reinterpret_cast<const void*>(a_function), a_prologue.data(), a_prologue.size()) != 0) {
            SKSE::log::error("{} starts differently than expected; not hooked", a_what);
            return nullptr;
        }

        detail::OriginalStub original{ a_prologue.data(), a_prologue.size(), a_function + a_prologue.size() };
        original.ready();

        auto& trampoline = SKSE::GetTrampoline();
        auto* result = reinterpret_cast<F*>(trampoline.allocate(original));
        trampoline.write_branch<5>(a_function, a_hook);
        SKSE::log::info("Hooked {} at SkyrimSE+{:X}", a_what, a_function - REL::Module::get().base());
        return result;
    }

}
