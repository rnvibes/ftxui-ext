#pragma once

#include "ftxui/ext/md/parser.h"
#include "ftxui/ext/md/theme.h"

#include <ftxui/dom/elements.hpp>

#include <vector>

namespace ftxui::ext
{

    // One entry of the rendered document, with the number of terminal rows it
    // occupies. Almost everything is a single row; a display equation and a
    // fenced code block are the exceptions, and a gutter has to know how far
    // to skip before the next number lines up.
    struct MdRow
    {
        ftxui::Element element;
        int height = 1;
        // Index into the MdDocument this row came from, so a caller can go from
        // "the cursor is on screen row 42" back to the block under it.
        int block = -1;
        // True when this row had content clipped at the right edge. Only code
        // can be truncated: everything else wraps.
        bool truncated = false;
        // Code fences are one navigable unit even though their renderer emits
        // one terminal row per source line. This lets a view virtualize their
        // visible rows without changing j/k semantics.
        bool atomic = false;
    };

    struct MdRenderOptions
    {
        // Code is never wrapped in the transcript -- its line breaks are the
        // content, and folding them would show something the model did not
        // write. The zoom view turns this on, where the reader has opened the
        // block deliberately and seeing all of it matters more than fidelity
        // to the original line structure.
        bool wrap_code = false;
    };

    // Pure document -> rows. No hit-testing state, no scroll wrapper, no
    // per-instance renderer object: the caller owns the viewport and decides
    // what to wrap the result in.
    std::vector<MdRow> render_markdown_rows(const MdDocument &doc, const MdTheme &theme,
                                 int viewport_width, MdRenderOptions options = {});

    // render_markdown_rows stacked into one Element, for callers that want no gutter.
    ftxui::Element render_markdown(const MdDocument &doc, const MdTheme &theme, int viewport_width);

} // namespace ftxui::ext
