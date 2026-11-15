#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

namespace InTheirShoes::GameClock {

    // Seconds the game has been running: real time, counted frame by frame (Lifecycle::OnFrame), leaving out time
    // the game is paused (menus that pause it, loading screens), time without frames (the window in the background
    // when the game stops there), and hitches. Real rather than the engine's frame time, which slow motion scales
    // but voices don't follow. Use it for gameplay timers; it means nothing across a restart.
    using TimePoint = double;

    namespace detail {
        // A gap between frames longer than this counts as paused, not played
        inline constexpr float kMaxFrameSeconds = 0.25f;
        inline std::atomic<TimePoint> now{ 0.0 };
        // Main thread only
        inline float frameSeconds{ 0.0f };
        inline std::chrono::steady_clock::time_point lastFrame{};
    }

    // Lifecycle::OnFrame, on the main thread
    inline void Tick() {
        const auto real = std::chrono::steady_clock::now();
        const bool first = detail::lastFrame == std::chrono::steady_clock::time_point{};
        const float gap = first ? 0.0f : std::chrono::duration<float>(real - detail::lastFrame).count();
        detail::lastFrame = real;

        auto* ui = RE::UI::GetSingleton();
        const bool paused = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
        detail::frameSeconds = paused ? 0.0f : std::min(gap, detail::kMaxFrameSeconds);
        detail::now.store(detail::now.load(std::memory_order_relaxed) + detail::frameSeconds, std::memory_order_release);
    }

    // Any thread
    inline TimePoint Now() { return detail::now.load(std::memory_order_acquire); }
    inline float SecondsSince(TimePoint a_time) { return static_cast<float>(Now() - a_time); }

    // How much the clock moved on the current frame. Main thread only.
    inline float FrameSeconds() { return detail::frameSeconds; }

}
