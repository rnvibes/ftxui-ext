#pragma once

#include "ftxui/ext/screen_grid.h"

#include <ftxui/dom/elements.hpp>

namespace ftxui::ext {

// Captures the child screen into out_grid, then inverts one cell when enabled.
// The pointed-to values are read only during rendering and must outlive the
// returned element, matching FTXUI's normal decorator/state pattern.
ftxui::Element CursorOverlay(ftxui::Element child, const int* row,
                             const int* col, const bool* enabled,
                             VisibleGrid* out_grid);

// As above, plus a cursorline: every cell of *row gets `line_bg` behind it.
// Painting after the child has drawn is what makes this work over content the
// overlay knows nothing about -- a bordered code block or a stacked equation
// highlights as readily as a line of prose, with no per-block cooperation.
// A default-constructed line_bg leaves the background alone.
// `line_rows` is how many terminal rows the cursorline covers: a unit that
// wraps to several rows highlights as the one thing it is.
// `invert` swaps the rows' foreground and background instead of setting
// `line_bg`, which is the one highlight that works without knowing whether the
// terminal's background is light or dark.
ftxui::Element CursorOverlay(ftxui::Element child, const int* row,
                             const int* col, const bool* enabled,
                             VisibleGrid* out_grid, ftxui::Color line_bg,
                             const bool* line_enabled, const int* line_rows,
                             bool invert = false);

}  // namespace ftxui::ext
