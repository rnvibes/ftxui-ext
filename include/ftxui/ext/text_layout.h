// ftxui-ext/include/ftxui/ext/text_layout.h — shared text-layout
// primitives for the markdown and org renderers.
//
// Both renderers split their inline content into styled words and lay them
// out with the same constructs: greedy word wrap (prose, list items, quotes),
// plain-string wrap (table cell measurement), table column sizing (natural
// width, shaved to per-word floors, then grown proportionally to span the
// budget), and element height measurement for multi-row blocks.
#pragma once

#include "ftxui/ext/code_highlighter.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace ftxui::ext
{

    // One styled word. Renderers produce these from their inline content;
    // wrap_words lays them out, keeping each word's style on its own text
    // element so neighbouring words never collapse into one decorated string.
    struct Word
    {
        std::string text;
        ftxui::Decorator style;
    };

    // Measures how many terminal rows an element intends to occupy. Layout
    // re-runs this during the real render pass; calling it early is
    // measurement, not a draw.
    inline int element_height(const ftxui::Element& element)
    {
        element->ComputeRequirement();
        return std::max(element->requirement().min_y, 1);
    }

    // Greedy word wrap. Each row is an hbox so neighbouring words keep their
    // own styles instead of collapsing into one decorated string.
    inline std::vector<ftxui::Element> wrap_words(std::vector<Word> words, int width)
    {
        width = std::max(width, 8);
        std::vector<ftxui::Element> rows;
        ftxui::Elements row;
        int used = 0;
        for (Word& word : words)
        {
            const int w = static_cast<int>(ftxui::string_width(word.text));
            if (!row.empty() && used + 1 + w > width)
            {
                rows.push_back(ftxui::hbox(std::move(row)));
                row.clear();
                used = 0;
            }
            if (!row.empty())
            {
                row.push_back(ftxui::text(" "));
                used += 1;
            }
            row.push_back(ftxui::text(word.text) | word.style);
            used += w;
        }
        if (!row.empty())
            rows.push_back(ftxui::hbox(std::move(row)));
        if (rows.empty())
            rows.push_back(ftxui::text(""));
        return rows;
    }

    // Plain-string word wrap, used where the content is measured in columns
    // (table cells) rather than styled word by word.
    inline std::vector<std::string> wrap_plain(std::string_view text, int width)
    {
        std::vector<std::string> lines;
        std::string line;
        std::size_t i = 0;
        while (i < text.size())
        {
            const std::size_t start = text.find_first_not_of(' ', i);
            if (start == std::string::npos)
                break;
            std::size_t end = text.find(' ', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string word(text.substr(start, end - start));
            const int w = static_cast<int>(ftxui::string_width(word));
            if (!line.empty() &&
                static_cast<int>(ftxui::string_width(std::string(line))) + 1 + w > width)
            {
                lines.push_back(line);
                line.clear();
            }
            if (!line.empty())
                line += ' ';
            line += word;
            i = end;
        }
        if (!line.empty() || lines.empty())
            lines.push_back(line);
        return lines;
    }

    // Table column sizing: measure the natural width, shave the widest column
    // (never below its longest single word, which is the narrowest a column
    // can get without clipping), then grow proportionally to span the budget —
    // otherwise a short table leaves a ragged gap instead of filling like
    // every other block does.
    inline std::vector<int> table_column_widths(
        const std::vector<std::vector<std::string>>& grid, std::size_t columns, int budget)
    {
        std::vector<int> natural(columns, 0), floor_width(columns, 1);
        for (const auto& row : grid)
        {
            for (std::size_t i = 0; i < columns; ++i)
            {
                natural[i] =
                    std::max(natural[i], static_cast<int>(ftxui::string_width(row[i])));
                for (const std::string& word : wrap_plain(row[i], 1))
                    floor_width[i] =
                        std::max(floor_width[i],
                                 static_cast<int>(ftxui::string_width(word)));
            }
        }

        std::vector<int> width = natural;
        const int padding = static_cast<int>(columns) * 2;
        int total = padding;
        for (int w : width)
            total += w;

        // Two passes: the first only takes from columns still above their
        // floor, so prose gives way before short label columns. If the floors
        // alone still overflow, the second shaves anyway rather than letting
        // the table run past the pane and get clipped.
        for (int pass = 0; pass < 2 && total > budget; ++pass)
        {
            const bool respect_floor = pass == 0;
            while (total > budget)
            {
                int victim = -1, widest = 0;
                for (std::size_t i = 0; i < columns; ++i)
                {
                    const int floor = respect_floor ? floor_width[i] : 1;
                    if (width[i] > floor && width[i] > widest)
                    {
                        widest = width[i];
                        victim = static_cast<int>(i);
                    }
                }
                if (victim < 0)
                    break;
                --width[static_cast<std::size_t>(victim)];
                --total;
            }
        }

        if (total < budget)
        {
            int extra = budget - total, natural_sum = 0, given = 0;
            for (int w : natural)
                natural_sum += w;
            for (std::size_t i = 0; i < columns; ++i)
            {
                const int share = natural_sum > 0
                                      ? (extra * natural[i]) / natural_sum
                                      : extra / static_cast<int>(columns);
                width[i] += share;
                given += share;
            }
            if (given < extra)
            {
                std::size_t widest = 0;
                for (std::size_t i = 1; i < columns; ++i)
                    if (width[i] > width[widest])
                        widest = i;
                width[widest] += extra - given;
            }
        }
        return width;
    }

    // The bordered code box (╭─╮ with a language label, syntax-highlighted
    // body, wrap-or-truncate handling) shared by the markdown and org
    // renderers so code looks identical everywhere.
    inline std::vector<ftxui::Element> code_box(
        std::string_view literal, std::string_view language, int width,
        bool wrap, bool& truncated, ftxui::Color border, ftxui::Color code_fg,
        ftxui::Color code_bg, ftxui::Color truncation,
        const ftxui::ext::SyntaxStyle& syntax_style)
    {
        const int outer = std::max(width, 12);
        const int inner = outer - 2;
        std::vector<ftxui::Element> rows;

        std::string top = "╭";
        if (!language.empty())
            top += " " + std::string(language) + " ";
        while (static_cast<int>(ftxui::string_width(top)) < outer - 1)
            top += "─";
        top += "╮";
        rows.push_back(ftxui::text(top) | ftxui::color(border));

        auto body_row = [&](ftxui::Element content)
        {
            return ftxui::hbox({
                ftxui::text("│") | ftxui::color(border),
                std::move(content) | ftxui::bgcolor(code_bg),
                ftxui::text("│") | ftxui::color(border),
            });
        };

    struct StyledToken
    {
        std::string_view text;
        ftxui::Decorator style;
    };

            const std::vector<ftxui::ext::HighlightSpan> spans =
                (!language.empty())
                    ? ftxui::ext::highlight_syntax(literal, language)
                    : std::vector<ftxui::ext::HighlightSpan>{};

            std::size_t start = 0;
            std::size_t span_idx = 0;
            while (start <= literal.size())
            {
                const std::size_t nl = literal.find('\n', start);
                const std::size_t line_end = nl == std::string::npos ? literal.size() : nl;
                const std::string_view line = literal.substr(start, line_end - start);

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
                                literal.substr(tok_start, tok_end - tok_start),
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
                            literal.substr(curr, line_end - curr),
                            ftxui::color(code_fg)
                        });
                    }
                }
                else
                {
                    line_tokens.push_back({line, ftxui::color(code_fg)});
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
                        std::string_view text;
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
                                const std::string_view sp_str = tok.text.substr(i, sp_end - i);
                                atoms.push_back({sp_str, tok.style, static_cast<int>(sp_str.size())});
                                i = sp_end;
                            }
                            else
                            {
                                size_t w_end = tok.text.find(' ', i);
                                if (w_end == std::string::npos) w_end = tok.text.size();
                                const std::string_view w_str = tok.text.substr(i, w_end - i);
                                atoms.push_back({w_str, tok.style, static_cast<int>(ftxui::string_width(std::string(w_str)))});
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
                            ftxui::text("  ") | ftxui::color(border),
                            ftxui::text(std::string(static_cast<size_t>(std::max(inner - 2, 0)), ' ')) |
                                ftxui::color(code_fg),
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
                                ftxui::color(border));

                            while (atom_i < atoms.size())
                            {
                                const auto &atom = atoms[atom_i];
                                if (used + atom.width <= budget)
                                {
                                    line_elems.push_back(ftxui::text(std::string(atom.text)) | atom.style);
                                    used += atom.width;
                                    ++atom_i;
                                }
                                else if (used == 0 && atom.width > budget)
                                {
                                    // Split oversized single word
                                    std::string cut(atom.text);
                                    while (!cut.empty() && static_cast<int>(ftxui::string_width(cut)) > budget)
                                    {
                                        cut.pop_back();
                                    }
                                    const int cut_w = static_cast<int>(ftxui::string_width(cut));
                                    line_elems.push_back(ftxui::text(cut) | atom.style);
                                    used += cut_w;
                                    atoms[atom_i].text = atom.text.substr(cut.size());
                                    atoms[atom_i].width = static_cast<int>(ftxui::string_width(std::string(atoms[atom_i].text)));
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
                    int line_width = static_cast<int>(ftxui::string_width(std::string(line)));
                    if (line_width > inner)
                    {
                        truncated = true;
                        Elements line_elems;
                        int cur_w = 0;
                        for (const auto &tok : line_tokens)
                        {
                            const int tok_w = static_cast<int>(ftxui::string_width(std::string(tok.text)));
                            if (cur_w + tok_w <= inner - 1)
                            {
                                line_elems.push_back(ftxui::text(std::string(tok.text)) | tok.style);
                                cur_w += tok_w;
                            }
                            else
                            {
                                std::string cut(tok.text);
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
                        line_elems.push_back(ftxui::text("›") | ftxui::bold | ftxui::color(truncation));
                        rows.push_back(body_row(ftxui::hbox(std::move(line_elems))));
                    }
                    else
                    {
                        Elements line_elems;
                        for (const auto &tok : line_tokens)
                        {
                            line_elems.push_back(ftxui::text(std::string(tok.text)) | tok.style);
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
                           ftxui::color(border));
        return rows;
    }

} // namespace ftxui::ext
