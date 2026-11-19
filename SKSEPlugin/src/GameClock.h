#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace InTheirShoes::GameClock {

    // Seconds the game has been running: real time, counted frame by frame (Lifecycle::OnFrame), leaving out time
    // the game is paused (menus that pause it, loading screens), time without frames (the window in the background
    // when the game stops there), and hitches. Use it for gameplay timers; it means nothing across a restart.
    //
    // Each frame's length is the engine timer's realTimeDelta: real time, taken before the slow-motion multiplier
    // (which voices don't follow). The engine resets the timer when it unpauses, so the frame after a pause or a
    // loading screen counts as one frame, not as the whole pause. The engine smooths it (a moving average), so a
    // hitch is spread over the next several frames and the clock lags it slightly; steady frames match the wall
    // clock within about 1% (checked in game).
    using TimePoint = double;

    namespace detail {
        // A frame longer than this is a hitch: it counts as this long
        inline constexpr float kMaxFrameSeconds = 0.25f;
        inline std::atomic<TimePoint> now{ 0.0 };
        // Main thread only
        inline float frameSeconds{ 0.0f };
    }

    // Lifecycle::OnFrame, on the main thread
    inline void Tick() {
        auto* timer = RE::BSTimer::GetSingleton();
        float delta = timer ? timer->realTimeDelta : 0.0f;
        if (!std::isfinite(delta) || delta < 0.0f) {
            delta = 0.0f;
        }

        auto* ui = RE::UI::GetSingleton();
        const bool paused = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
        detail::frameSeconds = paused ? 0.0f : std::min(delta, detail::kMaxFrameSeconds);
        detail::now.store(detail::now.load(std::memory_order_relaxed) + detail::frameSeconds, std::memory_order_release);
    }

    // Any thread
    inline TimePoint Now() { return detail::now.load(std::memory_order_acquire); }
    inline float SecondsSince(TimePoint a_time) { return static_cast<float>(Now() - a_time); }

    // How much the clock moved on the current frame. Main thread only.
    inline float FrameSeconds() { return detail::frameSeconds; }

}
