#include "ftxui/ext/cursor_overlay.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>
#include <utility>

namespace ftxui::ext {
namespace {

class CursorOverlayNode final : public ftxui::Node {
 public:
  CursorOverlayNode(ftxui::Element child, const int* row, const int* col,
                    const bool* enabled, VisibleGrid* out_grid,
                    ftxui::Color line_bg, const bool* line_enabled,
                    const int* line_rows, bool invert)
      : ftxui::Node({std::move(child)}),
        row_(row),
        col_(col),
        enabled_(enabled),
        out_grid_(out_grid),
        line_bg_(line_bg),
        line_enabled_(line_enabled),
        line_rows_(line_rows),
        invert_(invert) {}

  void ComputeRequirement() override {
    ftxui::Node::ComputeRequirement();
    requirement_ = children_[0]->requirement();
  }

  void SetBox(ftxui::Box box) override {
    ftxui::Node::SetBox(box);
    children_[0]->SetBox(box);
  }

  void Render(ftxui::Screen& screen) override {
    children_[0]->Render(screen);
    if (out_grid_) CaptureVisibleGrid(screen, box_, *out_grid_);

    // The cursorline is drawn first so the caret's inversion still reads
    // against it.
    if (line_enabled_ && *line_enabled_ && row_) {
      const int rows = line_rows_ ? std::max(*line_rows_, 1) : 1;
      for (int i = 0; i < rows; ++i) {
        const int y = box_.y_min + *row_ + i;
        if (y < box_.y_min || y > box_.y_max || y < 0 || y >= screen.dimy())
          continue;
        for (int x = std::max(box_.x_min, 0);
             x <= box_.x_max && x < screen.dimx(); ++x) {
          if (invert_)
            screen.PixelAt(x, y).inverted = !screen.PixelAt(x, y).inverted;
          else
            screen.PixelAt(x, y).background_color = line_bg_;
        }
      }
    }

    if (!enabled_ || !*enabled_ || !row_ || !col_) return;
    const int x = box_.x_min + *col_;
    const int y = box_.y_min + *row_;
    if (x < box_.x_min || x > box_.x_max || y < box_.y_min || y > box_.y_max)
      return;
    if (x < 0 || y < 0 || x >= screen.dimx() || y >= screen.dimy()) return;

    screen.PixelAt(x, y).inverted = !screen.PixelAt(x, y).inverted;
  }

 private:
  const int* row_;
  const int* col_;
  const bool* enabled_;
  VisibleGrid* out_grid_;
  ftxui::Color line_bg_;
  const bool* line_enabled_ = nullptr;
  const int* line_rows_ = nullptr;
  bool invert_ = false;
};

}  // namespace

ftxui::Element CursorOverlay(ftxui::Element child, const int* row,
                             const int* col, const bool* enabled,
                             VisibleGrid* out_grid) {
  return std::make_shared<CursorOverlayNode>(std::move(child), row, col,
                                              enabled, out_grid, ftxui::Color(),
                                              nullptr, nullptr, false);
}

ftxui::Element CursorOverlay(ftxui::Element child, const int* row,
                             const int* col, const bool* enabled,
                             VisibleGrid* out_grid, ftxui::Color line_bg,
                             const bool* line_enabled, const int* line_rows,
                             bool invert) {
  return std::make_shared<CursorOverlayNode>(std::move(child), row, col,
                                              enabled, out_grid, line_bg,
                                              line_enabled, line_rows, invert);
}

}  // namespace ftxui::ext
