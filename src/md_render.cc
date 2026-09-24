#include "ftxui/ext/md/render.h"

#include "ftxui/ext/latex_math.h"
#include "ftxui/ext/text_layout.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <utility>

namespace ftxui::ext
{
    namespace
    {

        using ftxui::Decorator;
        using ftxui::Element;
        using ftxui::Elements;
        using ftxui::ext::Word;

        Decorator style_for(const MdInline &span, const MdTheme &theme)
        {
            switch (span.kind)
            {
            case MdInlineKind::Bold:
                return ftxui::bold | ftxui::color(theme.bold_fg);
            case MdInlineKind::Italic:
                return ftxui::italic | ftxui::color(theme.italic_fg);
            case MdInlineKind::Code:
                return ftxui::color(theme.code_fg) | ftxui::bgcolor(theme.code_bg);
            case MdInlineKind::Math:
                return ftxui::color(span.emphasized ? theme.math_emphasis : theme.math_fg);
            case MdInlineKind::Text:
                break;
            }
            return ftxui::nothing;
        }

        // MdInline math is flattened to a single row of terminal glyphs here so it
        // wraps as ordinary words; only display math keeps the 2D stacked layout.
        std::string inline_text(const MdInline &span)
        {
            if (span.kind != MdInlineKind::Math)
                return span.text;
            return ftxui::ext::substitute_inline_math("$" + span.text + "$");
        }

        std::vector<Word> words_of(const std::vector<MdInline> &spans, const MdTheme &theme)
        {
            std::vector<Word> words;
            for (const MdInline &span : spans)
            {
                const Decorator style = style_for(span, theme);
                const std::string text = inline_text(span);
                std::size_t i = 0;
                while (i < text.size())
                {
                    const std::size_t start = text.find_first_not_of(" \t\n", i);
                    if (start == std::string::npos)
                        break;
                    std::size_t end = text.find_first_of(" \t\n", start);
                    if (end == std::string::npos)
                        end = text.size();
                    words.push_back({text.substr(start, end - start), style});
                    i = end;
                }
            }
            return words;
        }

        // Greedy wrap and element-height measurement come from the shared
        // text_layout.h so the org renderer lays text out identically.

        std::vector<WrappedRow> render_heading(const MdBlock &block, const MdTheme &theme, int width)
        {
            const ftxui::Color color = block.level <= 1   ? theme.heading1
                                       : block.level == 2 ? theme.heading2
                                                          : theme.heading3;
            std::vector<Word> words = words_of(block.spans, theme);
            for (Word &word : words)
                word.style = ftxui::bold | ftxui::color(color);
            std::vector<WrappedRow> rows = wrap_words_with_text(std::move(words), width);
            if (block.level <= 1)
                rows.push_back({ftxui::separator() | ftxui::color(color), ""});
            return rows;
        }

        // One element per terminal line, borders included, so every line a
        // block occupies gets its own gutter number. A single opaque element
        // would leave blanks in the number line, and relative numbers stop
        // being arithmetic you can trust the moment that happens.
        std::vector<WrappedRow> render_code(const MdBlock &block, const MdTheme &theme, int width,
                                            bool wrap, bool &truncated)
        {
            // the bordered box is shared with the org renderer
            return ftxui::ext::code_box_rows(block.literal, block.language, width, wrap,
                                             truncated, theme.border, theme.code_fg,
                                             theme.code_bg, theme.truncation,
                                             theme.syntax_style());
        }

        // The transcript's pipeline readout: a heavy box in its own colours,
        // one row per stage line. Deliberately not render_code -- a status
        // line must never read as code.
        std::vector<WrappedRow> render_stage(const MdBlock &block, const MdTheme &theme, int width)
        {
            return ftxui::ext::stage_box_rows(block.literal, "pipeline", width,
                                              theme.stage_border, theme.stage_fg,
                                              theme.stage_bg);
        }

        // Flatten a cell to plain text: a table cell is measured in columns, so
        // inline math has to become glyphs before any width arithmetic.
        std::string flatten(const std::vector<MdInline> &spans)
        {
            std::string out;
            for (const MdInline &span : spans)
                out += inline_text(span);
            return out;
        }

        struct TableUnit
        {
            Element element;
            int height = 1;
            std::string text;
        };

        // One unit per table row (plus the header and its rule), so the
        // cursor can step across a table and every row carries its own
        // number. A cell that wraps makes its row taller; the row is still
        // one unit.
        std::vector<TableUnit> render_table(const MdBlock &block,
                                            const MdTheme &theme, int width)
        {
            std::vector<TableUnit> units;
            const std::size_t columns = block.headers.empty()
                                            ? (block.rows.empty() ? 0 : block.rows[0].size())
                                            : block.headers.size();
            if (columns == 0)
                return units;

            std::vector<std::vector<std::string>> grid;
            auto flatten_row = [&](const MdTableRow &row)
            {
                std::vector<std::string> out(columns);
                for (std::size_t i = 0; i < columns && i < row.size(); ++i)
                    out[i] = flatten(row[i]);
                return out;
            };
            const bool has_header = !block.headers.empty();
            if (has_header)
                grid.push_back(flatten_row(block.headers));
            for (const MdTableRow &row : block.rows)
                grid.push_back(flatten_row(row));

            const std::vector<int> widths =
                table_column_widths(grid, columns, std::max(width, 20));

            auto row_element = [&](const std::vector<std::string> &cells, bool header) -> TableUnit
            {
                // Every cell wraps to its own column width; the row is as tall
                // as its tallest cell, and shorter cells are padded so the
                // columns stay aligned.
                std::vector<std::vector<std::string>> wrapped(columns);
                std::size_t tallest = 1;
                for (std::size_t i = 0; i < columns; ++i)
                {
                    wrapped[i] = wrap_plain(cells[i], widths[i]);
                    tallest = std::max(tallest, wrapped[i].size());
                }
                Elements lines;
                std::string row_joined;
                for (std::size_t c = 0; c < columns; ++c)
                {
                    if (c > 0) row_joined += " | ";
                    row_joined += cells[c];
                }
                for (std::size_t line = 0; line < tallest; ++line)
                {
                    Elements row;
                    for (std::size_t i = 0; i < columns; ++i)
                    {
                        std::string text = line < wrapped[i].size() ? wrapped[i][line] : "";
                        const int pad =
                            widths[i] - static_cast<int>(ftxui::string_width(text));
                        if (pad > 0)
                            text += std::string(static_cast<std::size_t>(pad), ' ');
                        Element cell = ftxui::text(" " + text + " ");
                        if (header)
                            cell = std::move(cell) | ftxui::bold |
                                   ftxui::color(theme.table_header);
                        row.push_back(std::move(cell));
                    }
                    lines.push_back(ftxui::hbox(std::move(row)));
                }
                return TableUnit{ftxui::vbox(std::move(lines)),
                                 static_cast<int>(tallest),
                                 std::move(row_joined)};
            };

            std::size_t index = 0;
            if (has_header)
            {
                units.push_back(row_element(grid[0], true));
                int total = 0;
                for (std::size_t i = 0; i < columns; ++i)
                    total += widths[i] + 2;
                std::string rule;
                for (int i = 0; i < total; ++i)
                    rule += "─";
                units.push_back({ftxui::text(rule) | ftxui::color(theme.table_border), 1, ""});
                index = 1;
            }
            for (; index < grid.size(); ++index)
                units.push_back(row_element(grid[index], false));
            return units;
        }

        // One row per item, so a list scrolls and numbers like prose rather
        // than being one opaque block.
        std::vector<WrappedRow> render_list(const MdBlock &block, const MdTheme &theme, int width)
        {
            std::vector<WrappedRow> rows;
            int number = block.start;
            for (const std::vector<MdInline> &item : block.items)
            {
                const std::string marker =
                    block.ordered ? std::to_string(number++) + ". " : "• ";
                const int indent = static_cast<int>(ftxui::string_width(marker));
                auto wrapped = wrap_words_with_text(words_of(item, theme), width - indent);
                for (std::size_t i = 0; i < wrapped.size(); ++i)
                {
                    Element lead = i == 0
                                       ? ftxui::text(marker) | ftxui::bold |
                                             ftxui::color(theme.list_marker)
                                       : ftxui::text(std::string(
                                             static_cast<std::size_t>(indent), ' '));
                    std::string row_text = (i == 0 ? marker : std::string(static_cast<std::size_t>(indent), ' ')) + wrapped[i].text;
                    rows.push_back({ftxui::hbox({std::move(lead), std::move(wrapped[i].element)}), std::move(row_text)});
                }
            }
            return rows;
        }

        std::vector<WrappedRow> render_quote(const MdBlock &block, const MdTheme &theme, int width)
        {
            std::vector<WrappedRow> rows;
            std::vector<Word> words = words_of(block.spans, theme);
            for (Word &word : words)
                word.style = ftxui::italic | ftxui::color(theme.blockquote);
            for (auto &line : wrap_words_with_text(std::move(words), width - 2))
            {
                std::string row_text = "│ " + line.text;
                rows.push_back({ftxui::hbox({
                    ftxui::text("│ ") | ftxui::bold | ftxui::color(theme.blockquote),
                    std::move(line.element),
                }), std::move(row_text)});
            }
            return rows;
        }

    } // namespace

    std::vector<MdRow> render_markdown_rows(const MdDocument &doc, const MdTheme &theme,
                                 int viewport_width, MdRenderOptions options)
    {
        std::vector<MdRow> rows;
        int index = -1;
        auto push_lines = [&](std::vector<WrappedRow> lines)
        {
            for (WrappedRow &line : lines)
                rows.push_back({std::move(line.element), 1, index, false, false, std::move(line.text)});
        };
        auto push_block = [&](Element element, std::string text, bool truncated = false)
        {
            const int height = element_height(element);
            rows.push_back({std::move(element), height, index, truncated, false, std::move(text)});
        };

        for (const MdBlock &block : doc)
        {
            ++index;
            if (!rows.empty())
                rows.push_back({ftxui::text(""), 1, index, false, false, ""});
            switch (block.kind)
            {
            case MdBlockKind::Heading:
                push_lines(render_heading(block, theme, viewport_width));
                break;
            case MdBlockKind::CodeBlock:
            {
                // One unit, one number: a fence reads as a single thing, and
                // numbering its interior lines would imply they are separately
                // addressable when they are not.
                bool truncated = false;
                auto lines = render_code(block, theme, viewport_width,
                                         options.wrap_code, truncated);
                for (WrappedRow &line : lines)
                    rows.push_back({std::move(line.element), 1, index, truncated, true, std::move(line.text)});
                break;
            }
            case MdBlockKind::Stage:
            {
                bool truncated = false;
                auto lines = render_stage(block, theme, viewport_width);
                for (WrappedRow &line : lines)
                    rows.push_back({std::move(line.element), 1, index, truncated, true, std::move(line.text)});
                break;
            }
            case MdBlockKind::Math:
                push_block(ftxui::ext::render_math(block.literal,
                                              ftxui::color(theme.math_fg),
                                              ftxui::ext::MathMode::Display),
                           block.literal);
                break;
            case MdBlockKind::Table:
                for (auto &[element, height, text] : render_table(block, theme, viewport_width))
                    rows.push_back({std::move(element), height, index, false, false, std::move(text)});
                break;
            case MdBlockKind::List:
                push_lines(render_list(block, theme, viewport_width));
                break;
            case MdBlockKind::Blockquote:
                push_lines(render_quote(block, theme, viewport_width));
                break;
            case MdBlockKind::Rule:
                rows.push_back({ftxui::separator() | ftxui::color(theme.border), 1, index, false, false, "---"});
                break;
            case MdBlockKind::Paragraph:
                push_lines(wrap_words_with_text(words_of(block.spans, theme), viewport_width));
                break;
            }
        }
        return rows;
    }

    Element render_markdown(const MdDocument &doc, const MdTheme &theme, int viewport_width)
    {
        Elements lines;
        for (MdRow &row : render_markdown_rows(doc, theme, viewport_width))
            lines.push_back(std::move(row.element));
        if (lines.empty())
            return ftxui::text("");
        return ftxui::vbox(std::move(lines));
    }

} // namespace ftxui::ext
