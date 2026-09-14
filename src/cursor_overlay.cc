#include "ftxui/ext/cursor_overlay.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>
#include <regex>
#include <utility>

namespace ftxui::ext {
namespace {

class CursorOverlayNode final : public ftxui::Node {
 public:
  CursorOverlayNode(ftxui::Element child, const int* row, const int* col,
                    const bool* enabled, VisibleGrid* out_grid,
                    ftxui::Color line_bg, const bool* line_enabled,
                    const int* line_rows, bool invert,
                    const std::string* search_pattern = nullptr,
                    const bool* hlsearch_enabled = nullptr,
                    ftxui::Color search_match_bg = ftxui::Color::Yellow,
                    ftxui::Color search_match_fg = ftxui::Color::Black,
                    const int* current_match_row = nullptr,
                    const int* current_match_col = nullptr,
                    const int* current_match_len = nullptr)
      : ftxui::Node({std::move(child)}),
        row_(row),
        col_(col),
        enabled_(enabled),
        out_grid_(out_grid),
        line_bg_(line_bg),
        line_enabled_(line_enabled),
        line_rows_(line_rows),
        invert_(invert),
        search_pattern_(search_pattern),
        hlsearch_enabled_(hlsearch_enabled),
        search_match_bg_(search_match_bg),
        search_match_fg_(search_match_fg),
        current_match_row_(current_match_row),
        current_match_col_(current_match_col),
        current_match_len_(current_match_len) {}

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

    if (hlsearch_enabled_ && *hlsearch_enabled_ && search_pattern_ &&
        !search_pattern_->empty() && out_grid_) {
      try {
        bool has_upper = false;
        for (char ch : *search_pattern_) {
          if (ch >= 'A' && ch <= 'Z') {
            has_upper = true;
            break;
          }
        }
        auto flags = std::regex::ECMAScript;
        if (!has_upper) {
          flags |= std::regex::icase;
        }
        std::regex re(*search_pattern_, flags);
        const std::string& text = out_grid_->text;
        auto begin = std::sregex_iterator(text.begin(), text.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
          std::smatch match = *it;
          const int start_off = static_cast<int>(match.position());
          const int len = static_cast<int>(match.length());
          if (len <= 0) continue;
          for (int i = 0; i < len; ++i) {
            int off = start_off + i;
            if (off >= static_cast<int>(text.size()) || text[static_cast<std::size_t>(off)] == '\n')
              continue;
            int r = 0, c = 0;
            RowColFor(*out_grid_, off, &r, &c);
            int x = box_.x_min + c;
            int y = box_.y_min + r;
            if (x < box_.x_min || x > box_.x_max || y < box_.y_min || y > box_.y_max)
              continue;
            if (x < 0 || y < 0 || x >= screen.dimx() || y >= screen.dimy())
              continue;

            screen.PixelAt(x, y).background_color = search_match_bg_;
            screen.PixelAt(x, y).foreground_color = search_match_fg_;
          }
        }
      } catch (...) {
      }
    }

    if (current_match_row_ && current_match_col_ && current_match_len_ &&
        *current_match_len_ > 0 && *current_match_row_ >= 0) {
      int r = *current_match_row_;
      int y = box_.y_min + r;
      if (y >= box_.y_min && y <= box_.y_max && y >= 0 && y < screen.dimy()) {
        for (int i = 0; i < *current_match_len_; ++i) {
          int x = box_.x_min + *current_match_col_ + i;
          if (x >= box_.x_min && x <= box_.x_max && x >= 0 && x < screen.dimx()) {
            screen.PixelAt(x, y).inverted = true;
          }
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
  const std::string* search_pattern_ = nullptr;
  const bool* hlsearch_enabled_ = nullptr;
  ftxui::Color search_match_bg_;
  ftxui::Color search_match_fg_;
  const int* current_match_row_ = nullptr;
  const int* current_match_col_ = nullptr;
  const int* current_match_len_ = nullptr;
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
                             bool invert,
                             const std::string* search_pattern,
                             const bool* hlsearch_enabled,
                             ftxui::Color search_match_bg,
                             ftxui::Color search_match_fg,
                             const int* current_match_row,
                             const int* current_match_col,
                             const int* current_match_len) {
  return std::make_shared<CursorOverlayNode>(
      std::move(child), row, col, enabled, out_grid, line_bg, line_enabled,
      line_rows, invert, search_pattern, hlsearch_enabled, search_match_bg,
      search_match_fg, current_match_row, current_match_col, current_match_len);
}

}  // namespace ftxui::ext
