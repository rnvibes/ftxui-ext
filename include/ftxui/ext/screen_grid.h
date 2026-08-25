#pragma once

#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>

#include <string>
#include <vector>

namespace ftxui::ext {

// A snapshot of what is actually drawn inside one region of an FTXUI screen,
// flattened into one string with one line per screen row. The column map keeps
// terminal columns distinct from UTF-8 byte offsets.
struct VisibleGrid {
  std::string text;
  std::vector<int> row_start;
  std::vector<int> row_len;
  std::vector<std::vector<int>> row_col_bytes;
};

// Captures into caller-owned storage. Existing capacities are retained across
// frames, which matters because this is normally called from a render node.
void CaptureVisibleGrid(const ftxui::Screen& screen, const ftxui::Box& box,
                        VisibleGrid& output);

int OffsetFor(const VisibleGrid& grid, int row, int col);
void RowColFor(const VisibleGrid& grid, int offset, int* row, int* col);

}  // namespace ftxui::ext
