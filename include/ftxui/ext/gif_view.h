#pragma once

// Animated GIF viewer. All frames are decoded up front (stb_image) and
// animated by advancing the frame index in OnAnimation, paced by each frame's
// delay. The current frame is drawn into a TFrameBuffer like BitmapView.

#include "ftxui/ext/bitmap_view.h"
#include "ftxui/ext/image_decode.h"

#include <ftxui/component/animation.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <filesystem>
#include <vector>

namespace ftxui::ext
{

    class GIFView : public ftxui::ComponentBase
    {
    public:
        static constexpr int kDefaultGrid = 80;

        static ftxui::Component Create(const std::filesystem::path &path,
                                       int grid = kDefaultGrid);
        static ftxui::Component Create(std::vector<Image> frames,
                                       std::vector<int> delays_ms,
                                       int grid = kDefaultGrid);

        virtual ~GIFView() = default;

        virtual std::size_t frame_count() const = 0;
        virtual std::size_t current_frame() const = 0;
        virtual ImageViewport &viewport() = 0;

        // Playback control
        virtual void Play() = 0;
        virtual void Pause() = 0;
        virtual void Stop() = 0;
        virtual void TogglePlay() = 0;
        virtual void SetLooping(bool looping) = 0;
        virtual bool IsPlaying() const = 0;
        virtual bool IsLooping() const = 0;
    };

} // namespace ftxui::ext
