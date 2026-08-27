#include "ftxui/ext/context_extractor.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ftxui::ext
{

    namespace
    {

        bool IsAbbreviation(std::string_view word)
        {
            static const char *const kAbbrevs[] = {
                "e.g.", "i.e.", "etc.", "mr.", "mrs.", "ms.", "dr.", "vs.",
                "prof.", "inc.", "corp.", "ltd.", "al.", "st.", "approx.",
                "dept.", "fig.", "figs.", "no.", "vol.", "v.", "gen."};
            std::string lower;
            lower.reserve(word.size());
            for (char c : word)
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

            for (const char *abbrev : kAbbrevs)
            {
                if (lower == abbrev)
                    return true;
            }
            return false;
        }

        std::string SpansToText(const std::vector<MdInline> &spans)
        {
            std::string result;
            for (const auto &span : spans)
                result += span.text;
            return result;
        }

    } // namespace

    ExtractedContext ExtractSentenceAt(std::string_view text, std::size_t char_index)
    {
        if (text.empty())
            return {"", 0, 0};

        std::vector<ExtractedContext> sentences;
        std::size_t n = text.size();
        std::size_t start = 0;

        // Skip initial whitespace
        while (start < n && std::isspace(static_cast<unsigned char>(text[start])))
            ++start;

        for (std::size_t i = start; i < n; ++i)
        {
            char c = text[i];
            if (c == '.' || c == '!' || c == '?')
            {
                // Check if part of a decimal number (e.g. 3.14 or v1.2)
                if (c == '.' && i > start && i + 1 < n &&
                    std::isdigit(static_cast<unsigned char>(text[i - 1])) &&
                    std::isdigit(static_cast<unsigned char>(text[i + 1])))
                {
                    continue;
                }

                // Check for abbreviation before '.'
                if (c == '.')
                {
                    // Find start of current word
                    std::size_t word_start = i;
                    while (word_start > start && !std::isspace(static_cast<unsigned char>(text[word_start - 1])))
                        --word_start;
                    std::string_view word = text.substr(word_start, (i + 1) - word_start);
                    if (IsAbbreviation(word))
                        continue;
                }

                // Absorb consecutive punctuation (ellipsis ..., ?!, etc.)
                while (i + 1 < n && (text[i + 1] == '.' || text[i + 1] == '!' || text[i + 1] == '?'))
                    ++i;

                // Absorb trailing quotes or closing brackets
                while (i + 1 < n && (text[i + 1] == '"' || text[i + 1] == '\'' ||
                                     text[i + 1] == ')' || text[i + 1] == ']' ||
                                     text[i + 1] == '}' || text[i + 1] == '>'))
                {
                    ++i;
                }

                // Sentence terminator should be followed by whitespace or EOF
                if (i + 1 == n || std::isspace(static_cast<unsigned char>(text[i + 1])))
                {
                    std::size_t end = i + 1;
                    std::string_view s = text.substr(start, end - start);
                    sentences.push_back({std::string(s), start, end});

                    // Advance start to next non-whitespace character
                    start = end;
                    while (start < n && std::isspace(static_cast<unsigned char>(text[start])))
                        ++start;
                    i = start > 0 ? start - 1 : 0;
                }
            }
        }

        // Remaining trailing sentence without punctuation
        if (start < n)
        {
            std::size_t end = n;
            while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
                --end;
            if (end > start)
            {
                std::string_view s = text.substr(start, end - start);
                sentences.push_back({std::string(s), start, end});
            }
        }

        if (sentences.empty())
            return {std::string(text), 0, text.size()};

        if (char_index == std::string_view::npos)
            return sentences.front();

        for (const auto &sent : sentences)
        {
            if (char_index >= sent.start_char && char_index <= sent.end_char)
                return sent;
        }

        // If char_index is beyond last sentence
        return sentences.back();
    }

    std::string FormatTableBlock(const MdBlock &block)
    {
        if (block.headers.empty() && block.rows.empty())
            return "";

        std::ostringstream oss;
        if (!block.headers.empty())
        {
            oss << "|";
            for (const auto &cell : block.headers)
                oss << " " << SpansToText(cell) << " |";
            oss << "\n|";
            for (std::size_t i = 0; i < block.headers.size(); ++i)
                oss << "---|";
            oss << "\n";
        }
        for (const auto &row : block.rows)
        {
            oss << "|";
            for (const auto &cell : row)
                oss << " " << SpansToText(cell) << " |";
            oss << "\n";
        }
        return oss.str();
    }

    std::string FormatCodeBlock(const MdBlock &block)
    {
        std::string res = "```" + block.language + "\n";
        res += block.literal;
        if (res.empty() || res.back() != '\n')
            res += "\n";
        res += "```";
        return res;
    }

    std::string FormatMathBlock(const MdBlock &block)
    {
        std::string res = "$$\n" + block.literal;
        if (res.empty() || res.back() != '\n')
            res += "\n";
        res += "$$";
        return res;
    }

    ExtractedContext ExtractBlockContext(const MdBlock &block, std::size_t char_index, int item_index)
    {
        switch (block.kind)
        {
        case MdBlockKind::Table:
            return {FormatTableBlock(block), 0, 0};
        case MdBlockKind::CodeBlock:
            return {FormatCodeBlock(block), 0, 0};
        case MdBlockKind::Math:
            return {FormatMathBlock(block), 0, 0};
        case MdBlockKind::List:
        {
            if (item_index >= 0 && static_cast<std::size_t>(item_index) < block.items.size())
            {
                std::string prefix = block.ordered
                                         ? std::to_string(block.start + item_index) + ". "
                                         : "- ";
                return {prefix + SpansToText(block.items[static_cast<std::size_t>(item_index)]), 0, 0};
            }
            std::string res;
            for (std::size_t i = 0; i < block.items.size(); ++i)
            {
                std::string prefix = block.ordered
                                         ? std::to_string(block.start + static_cast<int>(i)) + ". "
                                         : "- ";
                res += prefix + SpansToText(block.items[i]) + "\n";
            }
            return {res, 0, 0};
        }
        case MdBlockKind::Paragraph:
        case MdBlockKind::Heading:
        case MdBlockKind::Blockquote:
        case MdBlockKind::Rule:
        default:
        {
            std::string text = SpansToText(block.spans);
            return ExtractSentenceAt(text, char_index);
        }
        }
    }

} // namespace ftxui::ext
