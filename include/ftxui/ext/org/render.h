// ftxui-ext/include/ftxui/ext/org/render.h — pure org document -> rows.
//
// Mirrors ftxui::ext::md::render_rows: no viewport state, no hit-testing;
// the caller owns scrolling. Rows are ftxui elements with a height, the
// block index they came from, and code-fence atomicity, so a view can
// navigate units exactly like the markdown view does.
#pragma once

#include "ftxui/ext/org/doc.h"
#include "ftxui/ext/org/theme.h"

#include <ftxui/dom/elements.hpp>

#include <vector>

namespace ftxui::ext::org
{

    struct Row
    {
        ftxui::Element element;
        int height = 1;
        int block = -1;
        bool truncated = false;
        bool atomic = false;
    };

    struct RenderOptions
    {
        bool wrap_code = false;
    };

    // Pure document -> rows. Table cells wrap within their columns; prose
    // wraps to the viewport width; code is a bordered box (truncated unless
    // wrap_code).
    std::vector<Row> render_rows(const Document& doc, const Theme& theme,
                                 int viewport_width, RenderOptions options = {});

} // namespace ftxui::ext::org
