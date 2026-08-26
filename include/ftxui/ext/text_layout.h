// ftxui-ext/include/ftxui/ext/text_layout.h — shared text-layout
// primitives for the markdown and org renderers.
//
// Both renderers split their inline content into styled words and lay them
// out with the same constructs: greedy word wrap (prose, list items, quotes),
// plain-string wrap (table cell measurement), table column sizing (natural
// width, shaved to per-word floors, then grown proportionally to span the
// budget), and element height measurement for multi-row blocks.
#pragma once

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
    inline std::vector<std::string> wrap_plain(const std::string& text, int width)
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
            const std::string word = text.substr(start, end - start);
            const int w = static_cast<int>(ftxui::string_width(word));
            if (!line.empty() &&
                static_cast<int>(ftxui::string_width(line)) + 1 + w > width)
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

} // namespace ftxui::ext
