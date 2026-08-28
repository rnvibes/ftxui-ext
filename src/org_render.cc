// ftxui-ext/src/org/render.cc — org document -> styled rows.
//
// Layout constructs are the markdown renderer's, via the shared
// text_layout.h: greedy word wrap, measured table columns, hanging list
// indents, the bordered code box. Styling is org-flavored: headlines keep
// their stars and component colors (todo/priority/tags/timestamps) with the
// md heading treatment and a rule under level-1.
#include "ftxui/ext/org/render.h"

#include "ftxui/ext/text_layout.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace ftxui::ext
{
    namespace
    {

        using ftxui::Decorator;
        using ftxui::Element;
        using ftxui::Elements;
        using ftxui::ext::Word;
        using ftxui::ext::element_height;
        using ftxui::ext::table_column_widths;
        using ftxui::ext::wrap_plain;
        using ftxui::ext::wrap_words;

        Decorator style_for(const OrgInline& span, const OrgTheme& theme)
        {
            switch (span.kind)
            {
            case OrgInlineKind::Bold:
                return ftxui::bold | ftxui::color(theme.bold_fg);
            case OrgInlineKind::Italic:
                return ftxui::italic | ftxui::color(theme.italic_fg);
            case OrgInlineKind::Underline:
                return ftxui::underlined;
            case OrgInlineKind::Strike:
                return ftxui::strikethrough;
            case OrgInlineKind::Code:
            case OrgInlineKind::Verbatim:
                return ftxui::color(theme.code_fg) | ftxui::bgcolor(theme.code_bg);
            case OrgInlineKind::Link:
                return ftxui::underlined | ftxui::color(theme.link);
            case OrgInlineKind::Todo:
                return ftxui::bold | ftxui::color(theme.todo);
            case OrgInlineKind::Done:
                return ftxui::bold | ftxui::color(theme.done);
            case OrgInlineKind::Priority:
                return ftxui::color(theme.priority);
            case OrgInlineKind::Tag:
                return ftxui::color(theme.tag);
            case OrgInlineKind::TimestampActive:
                return ftxui::color(theme.timestamp_active);
            case OrgInlineKind::TimestampInactive:
                return ftxui::color(theme.timestamp_inactive);
            case OrgInlineKind::TimestampDiary:
                return ftxui::color(theme.timestamp_diary);
            case OrgInlineKind::PlanningKey:
                return ftxui::color(theme.planning_key);
            case OrgInlineKind::DrawerName:
                return ftxui::color(theme.drawer);
            case OrgInlineKind::PropertyKey:
                return ftxui::color(theme.property_key);
            case OrgInlineKind::KeywordKey:
                return ftxui::color(theme.keyword);
            case OrgInlineKind::Clock:
                return ftxui::color(theme.clock);
            case OrgInlineKind::Duration:
                return ftxui::color(theme.duration);
            case OrgInlineKind::LogState:
                return ftxui::color(theme.log_state);
            case OrgInlineKind::Computed:
                return ftxui::color(theme.computed);
            case OrgInlineKind::Text:
                break;
            }
            return ftxui::nothing;
        }

        // Append the whitespace-separated words of one text run. The text
        // is a view (source or arena); words are copied for ftxui.
        void push_words(std::vector<Word>& out, std::string_view text, Decorator style)
        {
            std::size_t i = 0;
            while (i < text.size())
            {
                const std::size_t start = text.find_first_not_of(" \t\n", i);
                if (start == std::string::npos)
                    break;
                std::size_t end = text.find_first_of(" \t\n", start);
                if (end == std::string::npos)
                    end = text.size();
                out.push_back({std::string(text.substr(start, end - start)), style});
                i = end;
            }
        }

        std::vector<Word> words_of(const std::pmr::vector<OrgInline>& spans, const OrgTheme& theme)
        {
            std::vector<Word> out;
            for (const OrgInline& span : spans)
                push_words(out, span.text, style_for(span, theme));
            return out;
        }

        // Headline words: plain text gets the md heading treatment (bold +
        // level color); component words keep their own colors.
        std::vector<Word> headline_words(const std::pmr::vector<OrgInline>& spans,
                                     const OrgTheme& theme, ftxui::Color color)
        {
            std::vector<Word> out;
            for (const OrgInline& span : spans)
            {
                Decorator style = ftxui::bold | ftxui::color(color);
                switch (span.kind)
                {
                case OrgInlineKind::Bold:
                case OrgInlineKind::Italic:
                case OrgInlineKind::Underline:
                case OrgInlineKind::Text:
                    break;
                default:
                    style = style_for(span, theme);
                    break;
                }
                push_words(out, span.text, style);
            }
            return out;
        }

        Elements render_heading(const OrgBlock& block, const OrgTheme& theme, int width)
        {
            const ftxui::Color color = block.level <= 1   ? theme.heading1
                                       : block.level == 2 ? theme.heading2
                                                          : theme.heading3;
            Elements rows = wrap_words(headline_words(block.spans, theme, color), width);
            if (block.level <= 1)
                rows.push_back(ftxui::separator() | ftxui::color(color));
            return rows;
        }

        std::vector<std::pair<Element, int>> render_table(const OrgBlock& block, const OrgTheme& theme,
                                                          int width)
        {
            std::vector<std::pair<Element, int>> units;
            const std::size_t columns =
                block.headers.empty() ? (block.rows.empty() ? 0 : block.rows[0].size())
                                      : block.headers.size();
            if (columns == 0)
                return units;

            std::vector<std::vector<std::string>> grid;
            std::vector<std::vector<bool>> computed;
            if (!block.headers.empty())
            {
                grid.push_back({});
                computed.push_back({});
                for (const OrgTableCell& cell : block.headers)
                {
                    grid.back().push_back(std::string(cell.text));
                    computed.back().push_back(cell.computed);
                }
            }
            for (const std::pmr::vector<OrgTableCell>& row : block.rows)
            {
                grid.push_back({});
                computed.push_back({});
                for (const OrgTableCell& cell : row)
                {
                    grid.back().push_back(std::string(cell.text));
                    computed.back().push_back(cell.computed);
                }
            }

            const std::vector<int> widths =
                table_column_widths(grid, columns, std::max(width, 20));

            auto row_element = [&](const std::vector<std::string>& cells,
                                   const std::vector<bool>& comp, bool header)
            {
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
                        else if (comp[i])
                            cell = std::move(cell) | ftxui::color(theme.computed);
                        row.push_back(std::move(cell));
                    }
                    lines.push_back(ftxui::hbox(std::move(row)));
                }
                return std::make_pair(ftxui::vbox(std::move(lines)),
                                      static_cast<int>(tallest));
            };

            std::size_t index = 0;
            if (!block.headers.empty())
            {
                units.push_back(row_element(grid[0], computed[0], true));
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
                units.push_back(row_element(grid[index], computed[index], false));
            return units;
        }

        Elements render_list(const OrgBlock& block, const OrgTheme& theme, int width)
        {
            Elements rows;
            for (const OrgListItem& item : block.items)
            {
                const int indent = static_cast<int>(ftxui::string_width(std::string(item.marker)));
                Elements wrapped = wrap_words(words_of(item.spans, theme), width - indent);
                for (std::size_t i = 0; i < wrapped.size(); ++i)
                {
                    Element lead =
                        i == 0
                            ? ftxui::text(std::string(item.marker)) | ftxui::bold |
                                  ftxui::color(theme.list_marker)
                            : ftxui::text(std::string(static_cast<std::size_t>(indent), ' '));
                    rows.push_back(ftxui::hbox({std::move(lead), std::move(wrapped[i])}));
                }
            }
            return rows;
        }

        Elements render_quote(const OrgBlock& block, const OrgTheme& theme, int width)
        {
            Elements rows;
            std::vector<Word> words = words_of(block.spans, theme);
            for (Word& word : words)
                word.style = ftxui::italic | ftxui::color(theme.blockquote);
            for (Element& line : wrap_words(std::move(words), width - 2))
            {
                rows.push_back(ftxui::hbox({
                    ftxui::text("│ ") | ftxui::bold | ftxui::color(theme.blockquote),
                    std::move(line),
                }));
            }
            return rows;
        }

    } // namespace

    std::vector<OrgRow> render_org_rows(const OrgDocument& doc, const OrgTheme& theme,
                                 int viewport_width, OrgRenderOptions options)
    {
        std::vector<OrgRow> rows;
        int index = -1;
        auto push_lines = [&](Elements lines)
        {
            for (Element& line : lines)
                rows.push_back({std::move(line), 1, index, false, false});
        };

        for (const OrgBlock& block : doc.blocks)
        {
            ++index;
            // spacing follows the source: only blocks the source separated
            // with a blank line get one (markdown always spaces its blocks)
            if (!rows.empty() && block.gap)
                rows.push_back({ftxui::text(""), 1, index, false, false});
            switch (block.kind)
            {
            case OrgBlockKind::Headline:
                push_lines(render_heading(block, theme, viewport_width));
                break;
            case OrgBlockKind::Paragraph:
            case OrgBlockKind::Keyword:
            case OrgBlockKind::DrawerOpen:
            case OrgBlockKind::DrawerEnd:
            case OrgBlockKind::Planning:
            case OrgBlockKind::Clock:
                push_lines(wrap_words(words_of(block.spans, theme), viewport_width));
                break;
            case OrgBlockKind::Table:
                for (auto& [element, height] : render_table(block, theme, viewport_width))
                    rows.push_back({std::move(element), height, index, false, false});
                break;
            case OrgBlockKind::List:
                push_lines(render_list(block, theme, viewport_width));
                break;
            case OrgBlockKind::Quote:
                push_lines(render_quote(block, theme, viewport_width));
                break;
            case OrgBlockKind::Src:
            case OrgBlockKind::Example:
            {
                bool truncated = false;
                Elements lines = ftxui::ext::code_box(
                    block.literal, block.language, viewport_width, options.wrap_code,
                    truncated, theme.border, theme.code_fg, theme.code_bg,
                    theme.truncation, theme.syntax_style());
                for (Element& line : lines)
                    rows.push_back({std::move(line), 1, index, truncated, true});
                break;
            }
            }
        }
        return rows;
    }

} // namespace ftxui::ext
