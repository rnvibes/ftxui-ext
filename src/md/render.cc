#include "ftxui/ext/md/render.h"

#include "ftxui/ext/latex_math.h"
#include "ftxui/ext/text_layout.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <utility>

namespace ftxui::ext::md
{
    namespace
    {

        using ftxui::Decorator;
        using ftxui::Element;
        using ftxui::Elements;
        using ftxui::ext::Word;

        Decorator style_for(const Inline &span, const Theme &theme)
        {
            switch (span.kind)
            {
            case InlineKind::Bold:
                return ftxui::bold | ftxui::color(theme.bold_fg);
            case InlineKind::Italic:
                return ftxui::italic | ftxui::color(theme.italic_fg);
            case InlineKind::Code:
                return ftxui::color(theme.code_fg) | ftxui::bgcolor(theme.code_bg);
            case InlineKind::Math:
                return ftxui::color(span.emphasized ? theme.math_emphasis : theme.math_fg);
            case InlineKind::Text:
                break;
            }
            return ftxui::nothing;
        }

        // Inline math is flattened to a single row of terminal glyphs here so it
        // wraps as ordinary words; only display math keeps the 2D stacked layout.
        std::string inline_text(const Inline &span)
        {
            if (span.kind != InlineKind::Math)
                return span.text;
            return ftxui::ext::substitute_inline_math("$" + span.text + "$");
        }

        std::vector<Word> words_of(const std::vector<Inline> &spans, const Theme &theme)
        {
            std::vector<Word> words;
            for (const Inline &span : spans)
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

        Elements render_heading(const Block &block, const Theme &theme, int width)
        {
            const ftxui::Color color = block.level <= 1   ? theme.heading1
                                       : block.level == 2 ? theme.heading2
                                                          : theme.heading3;
            std::vector<Word> words = words_of(block.spans, theme);
            for (Word &word : words)
                word.style = ftxui::bold | ftxui::color(color);
            Elements rows = wrap_words(std::move(words), width);
            if (block.level <= 1)
                rows.push_back(ftxui::separator() | ftxui::color(color));
            return rows;
        }


        struct StyledToken
        {
            std::string text;
            ftxui::Decorator style;
        };

        // One element per terminal line, borders included, so every line a
        // block occupies gets its own gutter number. A single opaque element
        // would leave blanks in the number line, and relative numbers stop
        // being arithmetic you can trust the moment that happens.
        Elements render_code(const Block &block, const Theme &theme, int width,
                             bool wrap, bool &truncated)
        {
            const int outer = std::max(width, 12);
            const int inner = outer - 2;
            Elements rows;

            std::string top = "╭";
            if (!block.language.empty())
                top += " " + block.language + " ";
            while (static_cast<int>(ftxui::string_width(top)) < outer - 1)
                top += "─";
            top += "╮";
            rows.push_back(ftxui::text(top) | ftxui::color(theme.border));

            auto body_row = [&](Element content)
            {
                return ftxui::hbox({
                    ftxui::text("│") | ftxui::color(theme.border),
                    std::move(content) | ftxui::bgcolor(theme.code_bg),
                    ftxui::text("│") | ftxui::color(theme.border),
                });
            };

            const auto syntax_style = theme.syntax_style();
            const std::vector<ftxui::ext::HighlightSpan> spans =
                (!block.language.empty())
                    ? ftxui::ext::highlight_syntax(block.literal, block.language)
                    : std::vector<ftxui::ext::HighlightSpan>{};

            std::size_t start = 0;
            std::size_t span_idx = 0;
            while (start <= block.literal.size())
            {
                const std::size_t nl = block.literal.find('\n', start);
                const std::size_t line_end = nl == std::string::npos ? block.literal.size() : nl;
                const std::string line = block.literal.substr(start, line_end - start);

                // Collect tokens for this line
                std::vector<StyledToken> line_tokens;
                if (!spans.empty())
                {
                    size_t curr = start;
                    while (span_idx < spans.size() && spans[span_idx].end_byte <= curr)
                    {
                        ++span_idx;
                    }
                    size_t s_idx = span_idx;
                    while (curr < line_end && s_idx < spans.size())
                    {
                        const auto &sp = spans[s_idx];
                        if (sp.start_byte >= line_end)
                            break;
                        const size_t tok_start = std::max(curr, sp.start_byte);
                        const size_t tok_end = std::min(line_end, sp.end_byte);
                        if (tok_end > tok_start)
                        {
                            line_tokens.push_back({
                                block.literal.substr(tok_start, tok_end - tok_start),
                                ftxui::ext::style_for_tag(sp.tag, syntax_style)
                            });
                            curr = tok_end;
                        }
                        if (sp.end_byte <= line_end)
                        {
                            ++s_idx;
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (curr < line_end)
                    {
                        line_tokens.push_back({
                            block.literal.substr(curr, line_end - curr),
                            ftxui::color(theme.code_fg)
                        });
                    }
                }
                else
                {
                    line_tokens.push_back({line, ftxui::color(theme.code_fg)});
                }

                if (wrap)
                {
                    // Compute base line indentation
                    const std::size_t first_non_space = line.find_first_not_of(' ');
                    const int indent_spaces = (first_non_space == std::string::npos)
                                                  ? 0
                                                  : std::min(static_cast<int>(first_non_space), (inner - 4) / 2);

                    // Break styled tokens into words / printable units
                    struct Atom
                    {
                        std::string text;
                        ftxui::Decorator style;
                        int width;
                    };
                    std::vector<Atom> atoms;
                    for (const auto &tok : line_tokens)
                    {
                        // Split token by space boundaries while preserving spaces
                        size_t i = 0;
                        while (i < tok.text.size())
                        {
                            if (tok.text[i] == ' ')
                            {
                                size_t sp_end = tok.text.find_first_not_of(' ', i);
                                if (sp_end == std::string::npos) sp_end = tok.text.size();
                                std::string sp_str = tok.text.substr(i, sp_end - i);
                                atoms.push_back({sp_str, tok.style, static_cast<int>(sp_str.size())});
                                i = sp_end;
                            }
                            else
                            {
                                size_t w_end = tok.text.find(' ', i);
                                if (w_end == std::string::npos) w_end = tok.text.size();
                                std::string w_str = tok.text.substr(i, w_end - i);
                                atoms.push_back({w_str, tok.style, static_cast<int>(ftxui::string_width(w_str))});
                                i = w_end;
                            }
                        }
                    }

                    // Layout sublines
                    bool is_first = true;
                    size_t atom_i = 0;
                    if (atoms.empty())
                    {
                        rows.push_back(body_row(ftxui::hbox({
                            ftxui::text("  ") | ftxui::color(theme.border),
                            ftxui::text(std::string(static_cast<size_t>(std::max(inner - 2, 0)), ' ')) |
                                ftxui::color(theme.code_fg),
                        })));
                    }
                    else
                    {
                        while (atom_i < atoms.size())
                        {
                            const int prefix_w = is_first ? 2 : (2 + indent_spaces);
                            const int budget = std::max(inner - prefix_w, 1);
                            int used = 0;
                            Elements line_elems;
                            line_elems.push_back(
                                ftxui::text(is_first ? "  " : ("↳ " + std::string(static_cast<size_t>(indent_spaces), ' '))) |
                                ftxui::color(theme.border));

                            while (atom_i < atoms.size())
                            {
                                const auto &atom = atoms[atom_i];
                                if (used + atom.width <= budget)
                                {
                                    line_elems.push_back(ftxui::text(atom.text) | atom.style);
                                    used += atom.width;
                                    ++atom_i;
                                }
                                else if (used == 0 && atom.width > budget)
                                {
                                    // Split oversized single word
                                    std::string cut = atom.text;
                                    while (!cut.empty() && static_cast<int>(ftxui::string_width(cut)) > budget)
                                    {
                                        cut.pop_back();
                                    }
                                    const int cut_w = static_cast<int>(ftxui::string_width(cut));
                                    line_elems.push_back(ftxui::text(cut) | atom.style);
                                    used += cut_w;
                                    atoms[atom_i].text = atom.text.substr(cut.size());
                                    atoms[atom_i].width = static_cast<int>(ftxui::string_width(atoms[atom_i].text));
                                    break;
                                }
                                else
                                {
                                    break;
                                }
                            }

                            const int pad = inner - prefix_w - used;
                            if (pad > 0)
                            {
                                line_elems.push_back(ftxui::text(std::string(static_cast<size_t>(pad), ' ')));
                            }
                            rows.push_back(body_row(ftxui::hbox(std::move(line_elems))));
                            is_first = false;
                        }
                    }
                }
                else
                {
                    int line_width = static_cast<int>(ftxui::string_width(line));
                    if (line_width > inner)
                    {
                        truncated = true;
                        Elements line_elems;
                        int cur_w = 0;
                        for (const auto &tok : line_tokens)
                        {
                            const int tok_w = static_cast<int>(ftxui::string_width(tok.text));
                            if (cur_w + tok_w <= inner - 1)
                            {
                                line_elems.push_back(ftxui::text(tok.text) | tok.style);
                                cur_w += tok_w;
                            }
                            else
                            {
                                std::string cut = tok.text;
                                while (!cut.empty() && cur_w + static_cast<int>(ftxui::string_width(cut)) > inner - 1)
                                {
                                    cut.pop_back();
                                }
                                if (!cut.empty())
                                {
                                    line_elems.push_back(ftxui::text(cut) | tok.style);
                                }
                                break;
                            }
                        }
                        line_elems.push_back(ftxui::text("›") | ftxui::bold | ftxui::color(theme.truncation));
                        rows.push_back(body_row(ftxui::hbox(std::move(line_elems))));
                    }
                    else
                    {
                        Elements line_elems;
                        for (const auto &tok : line_tokens)
                        {
                            line_elems.push_back(ftxui::text(tok.text) | tok.style);
                        }
                        const int pad = inner - line_width;
                        if (pad > 0)
                        {
                            line_elems.push_back(ftxui::text(std::string(static_cast<size_t>(pad), ' ')));
                        }
                        rows.push_back(body_row(ftxui::hbox(std::move(line_elems))));
                    }
                }

                if (nl == std::string::npos)
                    break;
                start = nl + 1;
            }

            rows.push_back(ftxui::text("╰" + [&]
                                       {
                                           std::string dashes;
                                           for (int i = 0; i < inner; ++i)
                                               dashes += "─";
                                           return dashes;
                                       }() + "╯") |
                           ftxui::color(theme.border));
            return rows;
        }

        // Flatten a cell to plain text: a table cell is measured in columns, so
        // inline math has to become glyphs before any width arithmetic.
        std::string flatten(const std::vector<Inline> &spans)
        {
            std::string out;
            for (const Inline &span : spans)
                out += inline_text(span);
            return out;
        }


        // One unit per table row (plus the header and its rule), so the
        // cursor can step across a table and every row carries its own
        // number. A cell that wraps makes its row taller; the row is still
        // one unit.
        std::vector<std::pair<Element, int>> render_table(const Block &block,
                                                          const Theme &theme, int width)
        {
            std::vector<std::pair<Element, int>> units;
            const std::size_t columns = block.headers.empty()
                                            ? (block.rows.empty() ? 0 : block.rows[0].size())
                                            : block.headers.size();
            if (columns == 0)
                return units;

            std::vector<std::vector<std::string>> grid;
            auto flatten_row = [&](const TableRow &row)
            {
                std::vector<std::string> out(columns);
                for (std::size_t i = 0; i < columns && i < row.size(); ++i)
                    out[i] = flatten(row[i]);
                return out;
            };
            const bool has_header = !block.headers.empty();
            if (has_header)
                grid.push_back(flatten_row(block.headers));
            for (const TableRow &row : block.rows)
                grid.push_back(flatten_row(row));

            const std::vector<int> widths =
                table_column_widths(grid, columns, std::max(width, 20));

            auto row_element = [&](const std::vector<std::string> &cells, bool header)
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
                return std::make_pair(ftxui::vbox(std::move(lines)),
                                      static_cast<int>(tallest));
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
                units.push_back({ftxui::text(rule) | ftxui::color(theme.table_border), 1});
                index = 1;
            }
            for (; index < grid.size(); ++index)
                units.push_back(row_element(grid[index], false));
            return units;
        }

        // One row per item, so a list scrolls and numbers like prose rather
        // than being one opaque block.
        Elements render_list(const Block &block, const Theme &theme, int width)
        {
            Elements rows;
            int number = block.start;
            for (const std::vector<Inline> &item : block.items)
            {
                const std::string marker =
                    block.ordered ? std::to_string(number++) + ". " : "• ";
                const int indent = static_cast<int>(ftxui::string_width(marker));
                Elements wrapped = wrap_words(words_of(item, theme), width - indent);
                for (std::size_t i = 0; i < wrapped.size(); ++i)
                {
                    Element lead = i == 0
                                       ? ftxui::text(marker) | ftxui::bold |
                                             ftxui::color(theme.list_marker)
                                       : ftxui::text(std::string(
                                             static_cast<std::size_t>(indent), ' '));
                    rows.push_back(ftxui::hbox({std::move(lead), std::move(wrapped[i])}));
                }
            }
            return rows;
        }

        Elements render_quote(const Block &block, const Theme &theme, int width)
        {
            Elements rows;
            std::vector<Word> words = words_of(block.spans, theme);
            for (Word &word : words)
                word.style = ftxui::italic | ftxui::color(theme.blockquote);
            for (Element &line : wrap_words(std::move(words), width - 2))
            {
                rows.push_back(ftxui::hbox({
                    ftxui::text("│ ") | ftxui::bold | ftxui::color(theme.blockquote),
                    std::move(line),
                }));
            }
            return rows;
        }

    } // namespace

    std::vector<Row> render_rows(const Document &doc, const Theme &theme,
                                 int viewport_width, RenderOptions options)
    {
        std::vector<Row> rows;
        int index = -1;
        auto push_lines = [&](Elements lines)
        {
            for (Element &line : lines)
                rows.push_back({std::move(line), 1, index, false});
        };
        auto push_block = [&](Element element, bool truncated = false)
        {
            const int height = element_height(element);
            rows.push_back({std::move(element), height, index, truncated});
        };

        for (const Block &block : doc)
        {
            ++index;
            if (!rows.empty())
                rows.push_back({ftxui::text(""), 1, index, false});
            switch (block.kind)
            {
            case BlockKind::Heading:
                push_lines(render_heading(block, theme, viewport_width));
                break;
            case BlockKind::CodeBlock:
            {
                // One unit, one number: a fence reads as a single thing, and
                // numbering its interior lines would imply they are separately
                // addressable when they are not.
                bool truncated = false;
                Elements lines = render_code(block, theme, viewport_width,
                                             options.wrap_code, truncated);
                for (Element &line : lines)
                    rows.push_back({std::move(line), 1, index, truncated, true});
                break;
            }
            case BlockKind::Math:
                push_block(ftxui::ext::render_math(block.literal,
                                              ftxui::color(theme.math_fg),
                                              ftxui::ext::MathMode::Display));
                break;
            case BlockKind::Table:
                for (auto &[element, height] : render_table(block, theme, viewport_width))
                    rows.push_back({std::move(element), height, index, false});
                break;
            case BlockKind::List:
                push_lines(render_list(block, theme, viewport_width));
                break;
            case BlockKind::Blockquote:
                push_lines(render_quote(block, theme, viewport_width));
                break;
            case BlockKind::Rule:
                rows.push_back({ftxui::separator() | ftxui::color(theme.border), 1, index, false});
                break;
            case BlockKind::Paragraph:
                push_lines(wrap_words(words_of(block.spans, theme), viewport_width));
                break;
            }
        }
        return rows;
    }

    Element render(const Document &doc, const Theme &theme, int viewport_width)
    {
        Elements lines;
        for (Row &row : render_rows(doc, theme, viewport_width))
            lines.push_back(std::move(row.element));
        if (lines.empty())
            return ftxui::text("");
        return ftxui::vbox(std::move(lines));
    }

} // namespace ftxui::ext::md
