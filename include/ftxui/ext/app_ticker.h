#pragma once

#include <chrono>
#include <functional>
#include <thread>

namespace ftxui::ext
{

// Timing only: the application decides what to advance and whether it needs
// a fast tick; this class owns the wait loop and its cancellation.
class AppTicker
{
public:
    using Tick = std::function<std::chrono::milliseconds()>;

    AppTicker() = default;
    ~AppTicker() { Stop(); }

    AppTicker(const AppTicker &) = delete;
    AppTicker &operator=(const AppTicker &) = delete;

    void Start(Tick tick);
    void Stop();

private:
    std::jthread worker_;
};

} // namespace ftxui::ext
