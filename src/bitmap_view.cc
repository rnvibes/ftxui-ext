#include "ftxui/ext/bitmap_view.h"

#include <ftxui/component/animation.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace ftxui::ext
{

    namespace
    {

        ftxui::Color ToColor(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                             std::uint8_t a)
        {
            // Composite over black; the buffer's Block mode uses a single color per
            // cell, so alpha is folded into the RGB.
            const auto blend = [a](std::uint8_t c)
            {
                return static_cast<std::uint8_t>((static_cast<unsigned>(c) * a) / 255);
            };
            return ftxui::Color::RGB(blend(r), blend(g), blend(b));
        }

    } // namespace

    void DrawImageScaled(TFrameBuffer &buffer, const Image &image, double scale,
                         int pan_x, int pan_y)
    {
        buffer.clear();
        if (image.width <= 0 || image.height <= 0 || scale <= 0.0 || image.pixels.empty())
            return;

        const int pane_w = buffer.width();
        const int pane_h = buffer.height();
        // Height is divided by the cell aspect (cells are ~2:1 tall:wide), so the
        // content keeps the image's true aspect on screen.
        const int content_w = std::max(1, static_cast<int>(image.width * scale));
        const int content_h =
            std::max(1, static_cast<int>(image.height * scale / kCellAspect));
        // Content origin: centered when smaller than the viewport (fit case),
        // otherwise the pan offsets position the visible window (native case).
        const int origin_x = std::max(0, (pane_w - content_w) / 2) - pan_x;
        const int origin_y = std::max(0, (pane_h - content_h) / 2) - pan_y;

        // Nearest-neighbor point sampling
        for (int y = 0; y < pane_h; ++y)
        {
            const int img_y = static_cast<int>(static_cast<double>(y - origin_y) *
                                               kCellAspect / scale);
            if (img_y < 0 || img_y >= image.height)
                continue;
            for (int x = 0; x < pane_w; ++x)
            {
                const int img_x =
                    static_cast<int>(static_cast<double>(x - origin_x) / scale);
                if (img_x < 0 || img_x >= image.width)
                    continue;
                const std::size_t index =
                    static_cast<std::size_t>(img_y) * image.width * 4 +
                    static_cast<std::size_t>(img_x) * 4;
                const auto *p = &image.pixels[index];
                buffer.draw_point(x, y, ToColor(p[0], p[1], p[2], p[3]));
            }
        }
    }

    double ImageFitScale(const Image &image, int pane_width, int pane_height)
    {
        if (image.width <= 0 || image.height <= 0)
            return 1.0;
        if (pane_width <= 0 || pane_height <= 0)
            return 1.0;
        const double fit =
            std::min(static_cast<double>(pane_width) / image.width,
                     static_cast<double>(pane_height) * kCellAspect / image.height);
        return std::max(fit, kDefaultViewScale);
    }

    double PureFitScale(const Image &image, int pane_width, int pane_height)
    {
        if (image.width <= 0 || image.height <= 0)
            return 1.0;
        if (pane_width <= 0 || pane_height <= 0)
            return 1.0;
        return std::min(static_cast<double>(pane_width) / image.width,
                        static_cast<double>(pane_height) * kCellAspect /
                            image.height);
    }

    void ImageViewport::Draw(TFrameBuffer &buffer, const Image &image)
    {
        const int pane_w = buffer.width();
        const int pane_h = buffer.height();

        if (pane_w != last_pane_w_ || pane_h != last_pane_h_)
        {
            recentered_ = false;
            last_pane_w_ = pane_w;
            last_pane_h_ = pane_h;
            ftxui::animation::RequestAnimationFrame();
        }

        fit_scale_ = PureFitScale(image, pane_w, pane_h);
        view_scale_ = ImageFitScale(image, pane_w, pane_h);
        const double scale = view_scale_ * zoom_;
        const int content_w = std::max(1, static_cast<int>(image.width * scale));
        const int content_h =
            std::max(1, static_cast<int>(image.height * scale / kCellAspect));

        max_offset_x_ = std::max(0, content_w - pane_w);
        max_offset_y_ = std::max(0, content_h - pane_h);

        if (!recentered_)
        {
            offset_x_ = max_offset_x_ / 2;
            offset_y_ = max_offset_y_ / 2;
            recentered_ = true;
        }
        else
        {
            offset_x_ = std::clamp(offset_x_, 0, max_offset_x_);
            offset_y_ = std::clamp(offset_y_, 0, max_offset_y_);
        }

        DrawImageScaled(buffer, image, scale, offset_x_, offset_y_);
    }

    void ImageViewport::Pan(int dx, int dy)
    {
        offset_x_ = std::clamp(offset_x_ + dx, 0, max_offset_x_);
        offset_y_ = std::clamp(offset_y_ + dy, 0, max_offset_y_);
    }

    void ImageViewport::PanLeft(int count)
    {
        Pan(-4 * count, 0);
    }

    void ImageViewport::PanRight(int count)
    {
        Pan(4 * count, 0);
    }

    void ImageViewport::PanUp(int count)
    {
        Pan(0, -2 * count);
    }

    void ImageViewport::PanDown(int count)
    {
        Pan(0, 2 * count);
    }

    void ImageViewport::PageUp(int count)
    {
        Pan(0, -std::max(1, last_pane_h_ / 2) * count);
    }

    void ImageViewport::PageDown(int count)
    {
        Pan(0, std::max(1, last_pane_h_ / 2) * count);
    }

    void ImageViewport::PanHome()
    {
        offset_x_ = 0;
    }

    void ImageViewport::PanEnd()
    {
        offset_x_ = max_offset_x_;
    }

    void ImageViewport::PanTop()
    {
        offset_y_ = 0;
    }

    void ImageViewport::PanBottom()
    {
        offset_y_ = max_offset_y_;
    }

    bool ImageViewport::OnEvent(ftxui::Event event)
    {
        if (event == ftxui::Event::Character('=') ||
            event == ftxui::Event::Character('+'))
        {
            ZoomIn();
            return true;
        }
        if (event == ftxui::Event::Character('-') ||
            event == ftxui::Event::Character('_'))
        {
            ZoomOut();
            return true;
        }
        if (event == ftxui::Event::Character('0'))
        {
            ResetZoom();
            return true;
        }
        if (event == ftxui::Event::Character('h') || event == ftxui::Event::ArrowLeft)
        {
            PanLeft();
            return true;
        }
        if (event == ftxui::Event::Character('l') || event == ftxui::Event::ArrowRight)
        {
            PanRight();
            return true;
        }
        if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown)
        {
            PanDown();
            return true;
        }
        if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp)
        {
            PanUp();
            return true;
        }
        if (event == ftxui::Event::PageDown || event == ftxui::Event::CtrlD)
        {
            PageDown();
            return true;
        }
        if (event == ftxui::Event::PageUp || event == ftxui::Event::CtrlU)
        {
            PageUp();
            return true;
        }
        if (event.is_mouse())
        {
            const auto mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::WheelLeft)
            {
                PanLeft(1);
                return true;
            }
            if (mouse.button == ftxui::Mouse::WheelRight)
            {
                PanRight(1);
                return true;
            }
            if (mouse.button == ftxui::Mouse::WheelDown)
            {
                if (mouse.shift)
                    PanRight(1);
                else
                    PageDown(1);
                return true;
            }
            if (mouse.button == ftxui::Mouse::WheelUp)
            {
                if (mouse.shift)
                    PanLeft(1);
                else
                    PageUp(1);
                return true;
            }
            if (mouse.button == ftxui::Mouse::Left)
            {
                if (mouse.motion == ftxui::Mouse::Pressed)
                {
                    dragging_ = true;
                    last_mouse_x_ = mouse.x;
                    last_mouse_y_ = mouse.y;
                    return true;
                }
                if (mouse.motion == ftxui::Mouse::Moved && dragging_)
                {
                    const int dx = mouse.x - last_mouse_x_;
                    const int dy = mouse.y - last_mouse_y_;
                    last_mouse_x_ = mouse.x;
                    last_mouse_y_ = mouse.y;
                    Pan(-dx * 2, -dy * 2);
                    return true;
                }
                if (mouse.motion == ftxui::Mouse::Released)
                {
                    dragging_ = false;
                    return true;
                }
            }
        }
        return false;
    }

    void ImageViewport::ResetZoom()
    {
        zoom_ = std::clamp(fit_scale_ / view_scale_, kMinZoom, 1.0);
        recentered_ = false;
    }

    void ImageViewport::ZoomIn()
    {
        const double old_zoom = zoom_;
        zoom_ = std::min(zoom_ * kZoomStep, kMaxZoom);
        if (last_pane_w_ > 0 && last_pane_h_ > 0 && old_zoom > 0.0)
        {
            const double factor = zoom_ / old_zoom;
            const double center_x = offset_x_ + last_pane_w_ / 2.0;
            const double center_y = offset_y_ + last_pane_h_ / 2.0;
            offset_x_ = static_cast<int>(std::round(center_x * factor - last_pane_w_ / 2.0));
            offset_y_ = static_cast<int>(std::round(center_y * factor - last_pane_h_ / 2.0));
        }
    }

    void ImageViewport::ZoomOut()
    {
        const double old_zoom = zoom_;
        zoom_ = std::max(zoom_ / kZoomStep, kMinZoom);
        if (last_pane_w_ > 0 && last_pane_h_ > 0 && old_zoom > 0.0)
        {
            const double factor = zoom_ / old_zoom;
            const double center_x = offset_x_ + last_pane_w_ / 2.0;
            const double center_y = offset_y_ + last_pane_h_ / 2.0;
            offset_x_ = static_cast<int>(std::round(center_x * factor - last_pane_w_ / 2.0));
            offset_y_ = static_cast<int>(std::round(center_y * factor - last_pane_h_ / 2.0));
        }
    }

    namespace
    {

        class BitmapViewImpl final : public BitmapView
        {
        public:
            BitmapViewImpl(Image image, int grid)
                : buffer_(std::max(1, grid / 2), ftxui::Color::White,
                          TFrameBuffer::DrawMode::Block)
            {
                SetImage(std::move(image));
            }

            const Image &image() const override { return image_; }

            void SetImage(const Image &image) override
            {
                image_ = image;
                viewport_.Recenter();
            }

            ImageViewport &viewport() override { return viewport_; }

            ftxui::Element OnRender() override
            {
                const int box_w = viewport_box_.x_max - viewport_box_.x_min + 1;
                const int box_h = viewport_box_.y_max - viewport_box_.y_min + 1;
                if (box_w > 1 && box_h > 1)
                    buffer_.Resize(box_w, box_h);

                viewport_.Draw(buffer_, image_);
                return buffer_.render() | ftxui::reflect(viewport_box_);
            }

            bool OnEvent(ftxui::Event event) override
            {
                return viewport_.OnEvent(event);
            }

        private:
            Image image_;
            TFrameBuffer buffer_;
            ImageViewport viewport_;
            ftxui::Box viewport_box_;
        };

    } // namespace

    ftxui::Component BitmapView::Create(const Image &image, int grid)
    {
        return ftxui::Make<BitmapViewImpl>(image, grid);
    }

    ftxui::Component BitmapView::Create(const std::filesystem::path &path, int grid)
    {
        Image image;
        if (!LoadImage(path, image))
        {
            return BitmapView::Create(Image{}, grid);
        }
        return BitmapView::Create(std::move(image), grid);
    }

    ftxui::Component PNGView::Create(const std::filesystem::path &path, int grid)
    {
        return BitmapView::Create(path, grid);
    }

} // namespace ftxui::ext
