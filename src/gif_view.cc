#include "ftxui/ext/gif_view.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace ftxui::ext
{

    namespace
    {

        using Clock = std::chrono::steady_clock;

        class GIFViewImpl final : public GIFView
        {
        public:
            GIFViewImpl(std::vector<Image> frames, std::vector<int> delays_ms, int grid)
                : frames_(std::move(frames)),
                  delays_ms_(std::move(delays_ms)),
                  buffer_(std::max(1, grid / 2), ftxui::Color::White,
                          TFrameBuffer::DrawMode::Block)
            {
                if (delays_ms_.size() < frames_.size())
                {
                    delays_ms_.resize(frames_.size(), 100);
                }
                last_frame_tp_ = Clock::now();
            }

            std::size_t frame_count() const override { return frames_.size(); }
            std::size_t current_frame() const override { return frame_idx_; }
            ImageViewport &viewport() override { return viewport_; }

            void Play() override
            {
                if (playing_)
                    return;
                playing_ = true;
                last_frame_tp_ = Clock::now();
                ftxui::animation::RequestAnimationFrame();
            }

            void Pause() override
            {
                playing_ = false;
            }

            void Stop() override
            {
                playing_ = false;
                frame_idx_ = 0;
                viewport_.Recenter();
            }

            void TogglePlay() override
            {
                if (playing_)
                    Pause();
                else
                    Play();
            }

            void SetLooping(bool looping) override
            {
                looping_ = looping;
            }

            bool IsPlaying() const override { return playing_; }
            bool IsLooping() const override { return looping_; }

            void OnAnimation(ftxui::animation::Params & /*params*/) override
            {
                if (!playing_ || frames_.empty())
                    return;

                const auto now = Clock::now();
                const int delay = (frame_idx_ < delays_ms_.size()) ? delays_ms_[frame_idx_] : 100;
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_tp_).count();

                if (elapsed_ms >= delay)
                {
                    last_frame_tp_ = now;
                    if (frame_idx_ + 1 < frames_.size())
                    {
                        frame_idx_++;
                    }
                    else if (looping_)
                    {
                        frame_idx_ = 0;
                    }
                    else
                    {
                        playing_ = false;
                        return;
                    }
                }
                ftxui::animation::RequestAnimationFrame();
            }

            ftxui::Element OnRender() override
            {
                const int box_w = viewport_box_.x_max - viewport_box_.x_min + 1;
                const int box_h = viewport_box_.y_max - viewport_box_.y_min + 1;
                if (box_w > 1 && box_h > 1)
                    buffer_.Resize(box_w, box_h);

                if (!frames_.empty() && frame_idx_ < frames_.size())
                {
                    viewport_.Draw(buffer_, frames_[frame_idx_]);
                }
                return buffer_.render() | ftxui::reflect(viewport_box_);
            }

            bool OnEvent(ftxui::Event event) override
            {
                if (event == ftxui::Event::Character(' '))
                {
                    TogglePlay();
                    return true;
                }
                return viewport_.OnEvent(event);
            }

        private:
            std::vector<Image> frames_;
            std::vector<int> delays_ms_;
            std::size_t frame_idx_ = 0;
            bool playing_ = true;
            bool looping_ = true;
            Clock::time_point last_frame_tp_;

            TFrameBuffer buffer_;
            ImageViewport viewport_;
            ftxui::Box viewport_box_;
        };

    } // namespace

    ftxui::Component GIFView::Create(const std::filesystem::path &path, int grid)
    {
        std::vector<Image> frames;
        std::vector<int> delays_ms;
        if (!LoadGifFrames(path, frames, delays_ms))
        {
            Image single;
            if (LoadImage(path, single))
            {
                frames.push_back(std::move(single));
                delays_ms.push_back(100);
            }
        }
        return GIFView::Create(std::move(frames), std::move(delays_ms), grid);
    }

    ftxui::Component GIFView::Create(std::vector<Image> frames,
                                     std::vector<int> delays_ms,
                                     int grid)
    {
        return ftxui::Make<GIFViewImpl>(std::move(frames), std::move(delays_ms), grid);
    }

} // namespace ftxui::ext
