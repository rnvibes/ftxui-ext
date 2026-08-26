#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cstddef>
#include <string>

namespace ftxui::ext
{

    // A solid, mouse-addressable vertical scrollbar.
    //
    // FTXUI's vscroll_indicator is a single column of glyphs, which at terminal
    // resolution reads as a stray border, and it is decoration only: nothing can
    // ask it where its thumb is, so it can never be dragged. This owns the
    // geometry the renderer uses, which is the whole point -- hit-testing and
    // drag math agree with what the reader sees because they are computed from
    // the same numbers.
    //
    // `box` is filled by Render()'s own reflect(), so a bar that has never been
    // drawn simply contains no point and is never grabbed.
    struct Scrollbar
    {
        ftxui::Box box;
        int content = 0;  ///< total rows of content
        int viewport = 0; ///< rows visible at once
        int scroll = 0;   ///< first visible row
        int width = 1;    ///< columns; one matches the thin scrollbars across all panels

        [[nodiscard]] int max_scroll() const { return std::max(content - viewport, 0); }
        [[nodiscard]] bool scrollable() const { return max_scroll() > 0 && viewport > 0; }

        [[nodiscard]] int thumb_height() const
        {
            if (viewport <= 0)
                return 1;
            if (content <= viewport)
                return viewport;
            return std::clamp(viewport * viewport / content, 1, viewport);
        }

        [[nodiscard]] int thumb_top() const
        {
            const int travel = max_scroll();
            if (travel <= 0)
                return 0;
            return (viewport - thumb_height()) * scroll / travel;
        }

        [[nodiscard]] bool Contains(int x, int y) const { return box.Contain(x, y); }

        // Bar-relative row of a screen y, 0-based.
        [[nodiscard]] int RowAt(int y) const { return y - box.y_min; }

        // The scroll offset that would put the thumb's top edge at bar row
        // `top`. The inverse of thumb_top(), clamped to the travel.
        [[nodiscard]] int ScrollForThumbTop(int top) const
        {
            const int travel = max_scroll();
            const int span = std::max(viewport - thumb_height(), 0);
            if (travel <= 0 || span <= 0)
                return 0;
            return std::clamp(top * travel / span, 0, travel);
        }

        // Press at screen `y`: inside the thumb it starts a drag from where it
        // was grabbed, outside it jumps the thumb centred on the pointer first.
        // Returns the rows of thumb above the pointer, to be handed back to
        // ScrollForDrag for the rest of the gesture.
        [[nodiscard]] int GrabAt(int y)
        {
            const int row = RowAt(y);
            const int top = thumb_top();
            const int height = thumb_height();
            if (row >= top && row < top + height)
                return row - top;
            const int grab = height / 2;
            scroll = ScrollForThumbTop(row - grab);
            return grab;
        }

        [[nodiscard]] int ScrollForDrag(int y, int grab) const
        {
            return ScrollForThumbTop(RowAt(y) - grab);
        }

        // Non-const: it reflects its own box, so the geometry above is always
        // last frame's real extent rather than something a caller had to
        // remember to wire up.
        ftxui::Element Render(ftxui::Color thumb_color, ftxui::Color track_color)
        {
            const std::string cell(static_cast<std::size_t>(std::max(width, 1)), ' ');
            const int rows = std::max(viewport, 1);
            const int top = thumb_top();
            const int height = content <= viewport ? rows : thumb_height();
            ftxui::Elements cells;
            cells.reserve(static_cast<std::size_t>(rows));
            for (int i = 0; i < rows; ++i)
                cells.push_back(ftxui::text(cell) |
                                ftxui::bgcolor(i >= top && i < top + height
                                                   ? thumb_color
                                                   : track_color));
            return ftxui::vbox(std::move(cells)) | ftxui::reflect(box);
        }
    };

} // namespace ftxui::ext
