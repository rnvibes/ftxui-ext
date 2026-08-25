#pragma once

#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <vector>

namespace ftxui::ext {

// Shared terminal frame buffer. Renderables draw logical pixels into this buffer;
// this class is the only place that knows how logical pixels become FTXUI cells.
class TFrameBuffer {
 public:
  enum class DrawMode {
    Braille,
    Block,
  };

  explicit TFrameBuffer(int side, ftxui::Color on_color = ftxui::Color::White,
                        DrawMode mode = DrawMode::Braille);
  TFrameBuffer(int width_cells, int height_cells, ftxui::Color on_color,
               DrawMode mode);

  void Resize(int width_cells, int height_cells);

  int side() const { return width_cells_ / 2; }
  int grid_size() const { return width_cells_; }
  int width() const { return width_cells_; }
  int height() const { return height_cells_; }

  void set_draw_mode(DrawMode mode) { mode_ = mode; }

  void draw_point(int x, int y, bool state);
  void draw_point(int x, int y, ftxui::Color color);
  void draw_point_line(int x0, int y0, int x1, int y1, ftxui::Color color);
  void draw_block(int x, int y, ftxui::Color color);
  void draw_pixel(int x, int y, ftxui::Color color);
  void clear();
  void render(TFrameBuffer& target);

  ftxui::Element render();

 private:
  int width_cells_;
  int height_cells_;
  const ftxui::Color on_color_;
  DrawMode mode_;
  std::vector<ftxui::Color> pixels_;
  std::vector<bool> occupied_;
  ftxui::Canvas canvas_;
};

}  // namespace ftxui::ext
