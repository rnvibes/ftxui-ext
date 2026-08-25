#pragma once

// Image views rendering into a container-sized TFrameBuffer (Block mode: one
// colored terminal cell per image pixel). The buffer is sized to the pane
// from the reflected layout box, so the image fills the actual space.
//
// Scaling policy: the image is shown whole (fit, or upscaled to fill the
// pane) whenever it fits at 25% of the pane or more; larger images are viewed
// at the 25% floor — pixel-for-pixel rendering would blow past the terminal —
// and panned with Vim keys: j/k scroll down/up, h/l scroll left/right,
// gg/G to the vertical edges, <C-d>/<C-u> page, wheel.

#include "ftxui/ext/frame_buffer.h"
#include "ftxui/ext/image_decode.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <filesystem>
#include <memory>

namespace ftxui::ext
{

    // Viewing-scale floor: oversized images render at this fraction of their
    // native size instead of 1:1.
    inline constexpr double kDefaultViewScale = 0.25;

    // Terminal cells are about twice as tall as they are wide (a typical font
    // is ~8x16 px). Rendering one image pixel per cell therefore stretches the
    // image vertically by this factor; all image scaling divides the height by
    // it so images keep their true aspect on screen.
    inline constexpr double kCellAspect = 2.0;

    // Zoom step and bounds for ImageViewport.
    inline constexpr double kZoomStep = 1.5;
    inline constexpr double kMinZoom = 0.1;
    inline constexpr double kMaxZoom = 16.0;

    // Draws the visible window of `image` into the buffer's grid: the image is
    // scaled by `scale` and shifted by the pan offsets, centered in the pane when
    // it is smaller than the viewport.
    void DrawImageScaled(TFrameBuffer &buffer, const Image &image, double scale,
                         int pan_x, int pan_y);

    // Scaling policy shared by the image views. Images that fit the pane at
    // kDefaultViewScale or more are shown whole (fit/upscaled); larger images are
    // viewed at the kDefaultViewScale floor so the pan keys can pan them.
    double ImageFitScale(const Image &image, int pane_width, int pane_height);

    // The scale that fits the whole image in the pane (no viewing floor).
    double PureFitScale(const Image &image, int pane_width, int pane_height);

    // Pan/zoom state shared by the image views.
    class ImageViewport
    {
    public:
        ImageViewport() = default;

        // Re-centers the view on the next Draw (used when the image changes).
        void Recenter() { recentered_ = false; }

        // Recomputes the viewing scale (default view scale * zoom), configures the
        // pan offsets for the pane/content sizes, re-centers after an image or
        // zoom change, and draws the visible window into `buffer`.
        void Draw(TFrameBuffer &buffer, const Image &image);

        // Handles pan keys, wheel, and zoom keys. Returns true when consumed.
        bool OnEvent(ftxui::Event event);

        // Motion methods
        void Pan(int dx, int dy);
        void PanLeft(int count = 1);
        void PanRight(int count = 1);
        void PanUp(int count = 1);
        void PanDown(int count = 1);
        void PageUp(int count = 1);
        void PageDown(int count = 1);
        void PanHome();
        void PanEnd();
        void PanTop();
        void PanBottom();

        double zoom() const { return zoom_; }
        void ResetZoom(); // scale = fit the whole image in the pane
        void ZoomIn();
        void ZoomOut();

        int offset_x() const { return offset_x_; }
        int offset_y() const { return offset_y_; }
        int max_offset_x() const { return max_offset_x_; }
        int max_offset_y() const { return max_offset_y_; }

    private:
        double zoom_ = 1.0;
        double fit_scale_ = 1.0;
        double view_scale_ = 1.0;
        bool recentered_ = false;
        int last_pane_w_ = 0;
        int last_pane_h_ = 0;
        int offset_x_ = 0;
        int offset_y_ = 0;
        int max_offset_x_ = 0;
        int max_offset_y_ = 0;
        bool dragging_ = false;
        int last_mouse_x_ = 0;
        int last_mouse_y_ = 0;
    };

    class BitmapView : public ftxui::ComponentBase
    {
    public:
        static constexpr int kDefaultGrid = 80;

        static ftxui::Component Create(const Image &image, int grid = kDefaultGrid);
        static ftxui::Component Create(const std::filesystem::path &path,
                                       int grid = kDefaultGrid);

        virtual ~BitmapView() = default;

        virtual const Image &image() const = 0;
        virtual void SetImage(const Image &image) = 0;
        virtual ImageViewport &viewport() = 0;
    };

    class PNGView : public BitmapView
    {
    public:
        static ftxui::Component Create(const std::filesystem::path &path,
                                       int grid = BitmapView::kDefaultGrid);
    };

} // namespace ftxui::ext
