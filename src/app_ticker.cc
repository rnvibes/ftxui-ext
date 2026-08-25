#include "ftxui/ext/app_ticker.h"

#include <algorithm>

namespace ftxui::ext
{

void AppTicker::Start(Tick tick)
{
    Stop();
    worker_ = std::jthread([tick = std::move(tick)](std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            const auto delay = std::max(tick(), std::chrono::milliseconds(1));
            std::this_thread::sleep_for(delay);
        }
    });
}

void AppTicker::Stop()
{
    worker_.request_stop();
    if (worker_.joinable())
        worker_.join();
}

} // namespace ftxui::ext
