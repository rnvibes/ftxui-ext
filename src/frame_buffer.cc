#include "ftxui/ext/frame_buffer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ftxui::ext {

TFrameBuffer::TFrameBuffer(int side, ftxui::Color on_color, DrawMode mode)
    : width_cells_(side * 2),
      height_cells_(side * 2),
      on_color_(on_color),
      mode_(mode),
      pixels_(static_cast<std::size_t>(width_cells_) * height_cells_,
              ftxui::Color::Default),
      occupied_(pixels_.size(), false),
      canvas_(width_cells_, height_cells_) {
}

TFrameBuffer::TFrameBuffer(int width_cells, int height_cells,
                           ftxui::Color on_color, DrawMode mode)
    : width_cells_(std::max(1, width_cells)),
      height_cells_(std::max(1, height_cells)),
      on_color_(on_color),
      mode_(mode),
      pixels_(static_cast<std::size_t>(this->width_cells_) * this->height_cells_,
              ftxui::Color::Default),
      occupied_(pixels_.size(), false),
      canvas_(this->width_cells_, this->height_cells_) {
}

void TFrameBuffer::Resize(int width_cells, int height_cells) {
  width_cells = std::max(1, width_cells);
  height_cells = std::max(1, height_cells);
  if (width_cells == width_cells_ && height_cells == height_cells_) return;
  width_cells_ = width_cells;
  height_cells_ = height_cells;
  pixels_.assign(static_cast<std::size_t>(width_cells_) * height_cells_,
                 ftxui::Color::Default);
  occupied_.assign(pixels_.size(), false);
}

void TFrameBuffer::draw_point(int x, int y, bool state) {
  if (x < 0 || x >= width_cells_ || y < 0 || y >= height_cells_) return;
  const std::size_t index = static_cast<std::size_t>(y) * width_cells_ + x;
  occupied_[index] = state;
  if (state) pixels_[index] = on_color_;
}

void TFrameBuffer::draw_point(int x, int y, ftxui::Color color) {
  if (x < 0 || x >= width_cells_ || y < 0 || y >= height_cells_) return;
  const std::size_t index = static_cast<std::size_t>(y) * width_cells_ + x;
  occupied_[index] = true;
  pixels_[index] = color;
}

void TFrameBuffer::draw_point_line(int x0, int y0, int x1, int y1,
                                   ftxui::Color color) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    draw_point(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void TFrameBuffer::draw_block(int x, int y, ftxui::Color color) {
  draw_point(x, y, color);
  draw_point(x + 1, y, color);
}

void TFrameBuffer::draw_pixel(int x, int y, ftxui::Color color) {
  draw_point(x * 2, y * 2, color);
  draw_point(x * 2 + 1, y * 2, color);
  draw_point(x * 2, y * 2 + 1, color);
  draw_point(x * 2 + 1, y * 2 + 1, color);
}

void TFrameBuffer::clear() {
  std::fill(occupied_.begin(), occupied_.end(), false);
}

void TFrameBuffer::render(TFrameBuffer& /*target*/) {
}

ftxui::Element TFrameBuffer::render() {
  if (mode_ == DrawMode::Braille) {
    canvas_ = ftxui::Canvas(width_cells_, height_cells_);
    for (int y = 0; y < height_cells_; ++y) {
      for (int x = 0; x < width_cells_; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * width_cells_ + x;
        if (!occupied_[index]) continue;
        canvas_.DrawPoint(x, y, true, pixels_[index]);
      }
    }
  } else {
    canvas_ = ftxui::Canvas(width_cells_ * 2, height_cells_ * 4);
    for (int y = 0; y < height_cells_; ++y) {
      for (int x = 0; x < width_cells_; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * width_cells_ + x;
        if (!occupied_[index]) continue;
        const int px = x * 2;
        const int py = y * 4;
        canvas_.DrawBlock(px, py, true, pixels_[index]);
        canvas_.DrawBlock(px + 1, py, true, pixels_[index]);
        canvas_.DrawBlock(px, py + 2, true, pixels_[index]);
        canvas_.DrawBlock(px + 1, py + 2, true, pixels_[index]);
      }
    }
  }
  return ftxui::canvas(canvas_);
}

}  // namespace ftxui::ext
