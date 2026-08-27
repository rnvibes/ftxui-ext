#pragma once

#include "ftxui/ext/md/parser.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ftxui::ext
{

    struct ExtractedContext
    {
        std::string text;
        std::size_t start_char = 0;
        std::size_t end_char = 0;
    };

    // Extracts a single sentence from text around char_index.
    // If char_index is string::npos or out of bounds, returns the first or full sentence.
    // Accurately avoids splitting on common abbreviations (e.g., etc., i.e.) and decimal numbers.
    ExtractedContext ExtractSentenceAt(std::string_view text, std::size_t char_index);

    // Formats a markdown Table block back into pipe-delimited Markdown.
    std::string FormatTableBlock(const MdBlock &block);

    // Formats a markdown CodeBlock back into fenced Markdown.
    std::string FormatCodeBlock(const MdBlock &block);

    // Formats a Math block back into LaTeX $$...$$.
    std::string FormatMathBlock(const MdBlock &block);

    // Extracts context from a markdown Block:
    // - Table / CodeBlock / Math: full block content.
    // - Paragraph / Heading / Blockquote: sentence around char_index.
    // - List: list item at item_index (or full list if item_index < 0).
    ExtractedContext ExtractBlockContext(const MdBlock &block, std::size_t char_index = std::string_view::npos, int item_index = -1);

} // namespace ftxui::ext
