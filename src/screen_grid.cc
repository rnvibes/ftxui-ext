#include "ftxui/ext/screen_grid.h"

#include <algorithm>

namespace ftxui::ext {

void CaptureVisibleGrid(const ftxui::Screen& screen, const ftxui::Box& box,
                        VisibleGrid& output) {
  output.text.clear();
  output.row_start.clear();
  output.row_len.clear();

  if (box.x_max < box.x_min || box.y_max < box.y_min) {
    output.row_col_bytes.clear();
    return;
  }

  const int x_min = std::max(box.x_min, 0);
  const int x_max = std::min(box.x_max, screen.dimx() - 1);
  const int y_min = std::max(box.y_min, 0);
  const int y_max = std::min(box.y_max, screen.dimy() - 1);
  if (x_max < x_min || y_max < y_min) {
    output.row_col_bytes.clear();
    return;
  }

  const std::size_t row_count = static_cast<std::size_t>(y_max - y_min + 1);
  output.row_start.reserve(static_cast<std::size_t>(y_max - y_min + 1));
  output.row_len.reserve(static_cast<std::size_t>(y_max - y_min + 1));
  output.row_col_bytes.resize(row_count);

  for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
    const int y = y_min + static_cast<int>(row_index);
    std::vector<int>& col_bytes = output.row_col_bytes[row_index];
    col_bytes.clear();
    col_bytes.reserve(static_cast<std::size_t>(x_max - x_min + 1));

    if (row_index != 0) output.text.push_back('\n');
    const int row_start = static_cast<int>(output.text.size());
    output.row_start.push_back(row_start);

    for (int x = x_min; x <= x_max; ++x) {
      const std::string& glyph = screen.PixelAt(x, y).character;
      col_bytes.push_back(static_cast<int>(output.text.size()) - row_start);
      output.text += glyph.empty() ? " " : glyph;
    }
    while (output.text.size() > static_cast<std::size_t>(row_start) &&
           output.text.back() == ' ') {
      output.text.pop_back();
    }
    const int row_len = static_cast<int>(output.text.size()) - row_start;

    while (!col_bytes.empty() &&
           col_bytes.back() >= row_len) {
      col_bytes.pop_back();
    }
    col_bytes.push_back(row_len);

    output.row_len.push_back(row_len);
  }
}

int OffsetFor(const VisibleGrid& grid, int row, int col) {
  if (grid.row_start.empty()) return 0;
  const int rows = static_cast<int>(grid.row_start.size());
  row = std::clamp(row, 0, rows - 1);

  const std::vector<int>& cols = grid.row_col_bytes[row];
  if (cols.empty()) return grid.row_start[row];
  col = std::clamp(col, 0, static_cast<int>(cols.size()) - 1);
  return grid.row_start[row] + cols[col];
}

void RowColFor(const VisibleGrid& grid, int offset, int* row, int* col) {
  if (grid.row_start.empty()) {
    if (row) *row = 0;
    if (col) *col = 0;
    return;
  }
  const int rows = static_cast<int>(grid.row_start.size());
  offset = std::clamp(offset, 0, static_cast<int>(grid.text.size()));

  int found = rows - 1;
  for (int i = 0; i < rows; ++i) {
    if (offset < grid.row_start[i]) {
      found = i - 1;
      break;
    }
  }
  found = std::clamp(found, 0, rows - 1);
  if (row) *row = found;
  if (!col) return;

  const int byte_in_row =
      std::clamp(offset - grid.row_start[found], 0, grid.row_len[found]);
  const std::vector<int>& cols = grid.row_col_bytes[found];
  int resolved = 0;
  for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
    if (cols[i] <= byte_in_row)
      resolved = i;
    else
      break;
  }
  *col = resolved;
}

}  // namespace ftxui::ext
