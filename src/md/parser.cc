#include "ftxui/ext/md/parser.h"

#include "ftxui/ext/latex_math.h"

#include <cctype>
#include <cstdlib>

namespace ftxui::ext
{
    namespace
    {

        // Inverse of latex_math's escape_math_content, which backslash-prefixes
        // every ASCII punctuation character inside a math span so a markdown
        // parser returns it byte-for-byte. That protection is still worth having
        // (NormalizeMathDelimiters does the \(..\) -> $..$ rewriting in the same
        // pass), but the renderer needs the original LaTeX back.
        std::string unescape_math(std::string_view body)
        {
            std::string out;
            out.reserve(body.size());
            for (std::size_t i = 0; i < body.size(); ++i)
            {
                if (body[i] == '\\' && i + 1 < body.size() &&
                    std::ispunct(static_cast<unsigned char>(body[i + 1])))
                    ++i;
                out += body[i];
            }
            return out;
        }

        void push_text(std::vector<MdInline> &spans, std::string text)
        {
            if (text.empty())
                return;
            if (!spans.empty() && spans.back().kind == MdInlineKind::Text)
                spans.back().text += text;
            else
                spans.push_back({MdInlineKind::Text, std::move(text)});
        }

        // Emphasis/code scanner over one math-free run of text.
        void parse_emphasis(std::string_view text, std::vector<MdInline> &spans)
        {
            std::string pending;
            std::size_t i = 0;
            while (i < text.size())
            {
                if (text[i] == '\\' && i + 1 < text.size() &&
                    std::ispunct(static_cast<unsigned char>(text[i + 1])))
                {
                    pending += text[i + 1];
                    i += 2;
                    continue;
                }
                if (text[i] == '`')
                {
                    if (const std::size_t close = text.find('`', i + 1);
                        close != std::string_view::npos)
                    {
                        push_text(spans, std::move(pending));
                        pending.clear();
                        spans.push_back({MdInlineKind::Code,
                                         std::string(text.substr(i + 1, close - i - 1))});
                        i = close + 1;
                        continue;
                    }
                }
                if (text.compare(i, 2, "**") == 0)
                {
                    if (const std::size_t close = text.find("**", i + 2);
                        close != std::string_view::npos)
                    {
                        push_text(spans, std::move(pending));
                        pending.clear();
                        spans.push_back({MdInlineKind::Bold,
                                         std::string(text.substr(i + 2, close - i - 2))});
                        i = close + 2;
                        continue;
                    }
                }
                if (text[i] == '*' || text[i] == '_')
                {
                    if (const std::size_t close = text.find(text[i], i + 1);
                        close != std::string_view::npos && close > i + 1)
                    {
                        push_text(spans, std::move(pending));
                        pending.clear();
                        spans.push_back({MdInlineKind::Italic,
                                         std::string(text.substr(i + 1, close - i - 1))});
                        i = close + 1;
                        continue;
                    }
                }
                pending += text[i];
                ++i;
            }
            push_text(spans, std::move(pending));
        }

        // Math spans are claimed before emphasis so an equation's underscores
        // and asterisks are never read as markup.
        std::vector<MdInline> parse_inlines(std::string_view text)
        {
            std::vector<MdInline> spans;
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                auto span = ftxui::ext::find_math_span(text, cursor);
                if (!span)
                    break;
                const auto [open, end] = *span;
                // **$x$** -- claim the markers here rather than leaving them
                // for the emphasis scanner, which would find no partner for
                // them (the math is already consumed) and print them raw.
                std::size_t prose_end = open;
                std::size_t resume = end;
                bool emphasized = false;
                if (open >= cursor + 2 && text.compare(open - 2, 2, "**") == 0 &&
                    end + 2 <= text.size() && text.compare(end, 2, "**") == 0)
                {
                    emphasized = true;
                    prose_end = open - 2;
                    resume = end + 2;
                }
                parse_emphasis(text.substr(cursor, prose_end - cursor), spans);
                const std::size_t delim = text[open + 1] == '$' ? 2 : 1;
                spans.push_back({MdInlineKind::Math,
                                 unescape_math(text.substr(open + delim,
                                                           (end - delim) - (open + delim))),
                                 emphasized});
                cursor = resume;
            }
            parse_emphasis(text.substr(cursor), spans);
            return spans;
        }

        std::size_t fence_run(std::string_view line, char marker)
        {
            std::size_t n = 0;
            while (n < line.size() && line[n] == marker)
                ++n;
            return n;
        }

        std::string_view trim(std::string_view s)
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.remove_suffix(1);
            return s;
        }

        std::vector<std::string_view> split_lines(std::string_view source)
        {
            std::vector<std::string_view> lines;
            std::size_t start = 0;
            while (start <= source.size())
            {
                const std::size_t nl = source.find('\n', start);
                if (nl == std::string_view::npos)
                {
                    lines.push_back(source.substr(start));
                    break;
                }
                lines.push_back(source.substr(start, nl - start));
                start = nl + 1;
            }
            // A trailing newline terminates the last line rather than starting
            // an empty one — otherwise every code block gains a blank tail row.
            if (lines.size() > 1 && lines.back().empty())
                lines.pop_back();
            return lines;
        }

        // ── GFM pipe tables ────────────────────────────────────────────
        // Not CommonMark, but every model emits them, so "not in the spec" is
        // not a reason to drop the row on the floor.

        // Splits "| a | b |" into its cells, honouring backslash-escaped pipes
        // and ignoring the optional leading/trailing delimiters.
        std::vector<std::string> split_cells(std::string_view line)
        {
            std::vector<std::string> cells;
            std::string cell;
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '|')
                {
                    cell += '|';
                    ++i;
                    continue;
                }
                if (line[i] == '|')
                {
                    cells.push_back(std::string(trim(cell)));
                    cell.clear();
                    continue;
                }
                cell += line[i];
            }
            cells.push_back(std::string(trim(cell)));
            // A row written with the usual outer pipes yields an empty cell at
            // each end; drop those, but never a genuinely empty middle cell.
            if (!cells.empty() && cells.front().empty())
                cells.erase(cells.begin());
            if (!cells.empty() && cells.back().empty())
                cells.pop_back();
            return cells;
        }

        // The "|---|:--:|" line under the header is what makes a run of pipes
        // a table rather than prose that happens to contain them.
        bool is_delimiter_row(std::string_view line)
        {
            if (line.find('|') == std::string_view::npos)
                return false;
            bool saw_dash = false;
            for (char c : line)
            {
                if (c == '-')
                    saw_dash = true;
                else if (c != '|' && c != ':' && c != ' ' && c != '\t')
                    return false;
            }
            return saw_dash;
        }

        bool is_rule(std::string_view line)
        {
            if (line.size() < 3)
                return false;
            const char c = line.front();
            if (c != '-' && c != '*' && c != '_')
                return false;
            for (char ch : line)
                if (ch != c && ch != ' ')
                    return false;
            return true;
        }

        // "- x" / "* x" / "+ x" -> content offset, or 0 when not a bullet.
        std::size_t bullet_prefix(std::string_view line)
        {
            if (line.size() < 2)
                return 0;
            if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ')
                return 2;
            return 0;
        }

        // "12. x" -> content offset (and the number), or 0 when not ordered.
        std::size_t ordered_prefix(std::string_view line, int &number)
        {
            std::size_t i = 0;
            while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
                ++i;
            if (i == 0 || i + 1 >= line.size())
                return 0;
            if ((line[i] != '.' && line[i] != ')') || line[i + 1] != ' ')
                return 0;
            number = std::atoi(std::string(line.substr(0, i)).c_str());
            return i + 2;
        }

        // A paragraph that is nothing but one $$...$$ span becomes a display
        // block, so it gets the 2D stacked layout instead of being flattened
        // into a single prose row.
        bool whole_display_math(std::string_view text, std::string &latex_out)
        {
            auto span = ftxui::ext::find_math_span(text, 0);
            if (!span || span->first != 0 || span->second != text.size())
                return false;
            if (text.size() < 4 || text[1] != '$')
                return false;
            latex_out = unescape_math(text.substr(2, text.size() - 4));
            return true;
        }

    } // namespace

    MdDocument parse_markdown(std::string_view raw)
    {
        // Rewrites \(..\)/\[..\] to the dollar forms find_math_span scans for
        // and wraps bare LaTeX environments, which LLM output emits constantly.
        const std::string source = ftxui::ext::NormalizeMathDelimiters(raw);
        const std::vector<std::string_view> lines = split_lines(source);

        MdDocument doc;
        std::string paragraph;

        auto flush_paragraph = [&]
        {
            if (paragraph.empty())
                return;
            MdBlock block;
            if (std::string latex; whole_display_math(paragraph, latex))
            {
                block.kind = MdBlockKind::Math;
                block.literal = std::move(latex);
            }
            else
            {
                block.kind = MdBlockKind::Paragraph;
                block.spans = parse_inlines(paragraph);
            }
            doc.push_back(std::move(block));
            paragraph.clear();
        };

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const std::string_view line = lines[i];
            const std::string_view stripped = trim(line);

            const std::size_t ticks = fence_run(stripped, '`');
            const std::size_t tildes = fence_run(stripped, '~');
            if (ticks >= 3 || tildes >= 3)
            {
                flush_paragraph();
                const char marker = ticks >= 3 ? '`' : '~';
                const std::size_t width = ticks >= 3 ? ticks : tildes;
                MdBlock block;
                block.kind = MdBlockKind::CodeBlock;
                block.language = std::string(trim(stripped.substr(width)));
                // An unterminated fence runs to the end of the source: mid-stream
                // that is the common case, and showing the partial code block
                // beats dumping raw backticks until the closer arrives.
                for (++i; i < lines.size(); ++i)
                {
                    const std::string_view body = trim(lines[i]);
                    if (fence_run(body, marker) >= width && trim(body.substr(fence_run(body, marker))).empty())
                        break;
                    if (!block.literal.empty())
                        block.literal += '\n';
                    block.literal += std::string(lines[i]);
                }
                doc.push_back(std::move(block));
                continue;
            }

            if (stripped.empty())
            {
                flush_paragraph();
                continue;
            }

            // Table: a pipe row whose successor is a delimiter row. Checked
            // before paragraphs so the pipes are never swallowed as prose.
            if (stripped.find('|') != std::string_view::npos &&
                i + 1 < lines.size() && is_delimiter_row(trim(lines[i + 1])))
            {
                flush_paragraph();
                MdBlock block;
                block.kind = MdBlockKind::Table;
                for (const std::string &cell : split_cells(stripped))
                    block.headers.push_back(parse_inlines(cell));
                const std::size_t columns = block.headers.size();
                for (i += 2; i < lines.size(); ++i)
                {
                    const std::string_view row = trim(lines[i]);
                    if (row.empty() || row.find('|') == std::string_view::npos)
                        break;
                    MdTableRow cells;
                    for (const std::string &cell : split_cells(row))
                        cells.push_back(parse_inlines(cell));
                    // Ragged rows are normal in generated markdown; pad or
                    // trim to the header so the grid stays rectangular.
                    cells.resize(columns);
                    block.rows.push_back(std::move(cells));
                }
                --i; // the outer loop advances
                doc.push_back(std::move(block));
                continue;
            }

            if (is_rule(stripped))
            {
                flush_paragraph();
                MdBlock rule_block;
                rule_block.kind = MdBlockKind::Rule;
                doc.push_back(std::move(rule_block));
                continue;
            }

            // Blockquote: a contiguous run of "> " lines, joined like a
            // paragraph so it wraps as one.
            if (stripped.front() == '>')
            {
                flush_paragraph();
                std::string quoted;
                for (; i < lines.size(); ++i)
                {
                    std::string_view row = trim(lines[i]);
                    if (row.empty() || row.front() != '>')
                        break;
                    row.remove_prefix(1);
                    if (!quoted.empty())
                        quoted += ' ';
                    quoted += std::string(trim(row));
                }
                --i;
                MdBlock block;
                block.kind = MdBlockKind::Blockquote;
                block.spans = parse_inlines(quoted);
                doc.push_back(std::move(block));
                continue;
            }

            // List: a contiguous run of bullets or numbers. Each item is one
            // inline run; nested lists are a later feature.
            int first_number = 1;
            const std::size_t bullet = bullet_prefix(stripped);
            const std::size_t ordered = bullet ? 0 : ordered_prefix(stripped, first_number);
            if (bullet || ordered)
            {
                flush_paragraph();
                MdBlock block;
                block.kind = MdBlockKind::List;
                block.ordered = ordered != 0;
                block.start = block.ordered ? first_number : 1;
                for (; i < lines.size(); ++i)
                {
                    const std::string_view row = trim(lines[i]);
                    int ignored = 0;
                    const std::size_t offset = block.ordered ? ordered_prefix(row, ignored)
                                                             : bullet_prefix(row);
                    if (offset == 0)
                        break;
                    block.items.push_back(parse_inlines(row.substr(offset)));
                }
                --i;
                doc.push_back(std::move(block));
                continue;
            }

            if (stripped.front() == '#')
            {
                const std::size_t level = fence_run(stripped, '#');
                if (level <= 6 && level < stripped.size() &&
                    std::isspace(static_cast<unsigned char>(stripped[level])))
                {
                    flush_paragraph();
                    MdBlock block;
                    block.kind = MdBlockKind::Heading;
                    block.level = static_cast<int>(level);
                    block.spans = parse_inlines(trim(stripped.substr(level)));
                    doc.push_back(std::move(block));
                    continue;
                }
            }

            if (!paragraph.empty())
                paragraph += ' ';
            paragraph += std::string(stripped);
        }

        flush_paragraph();
        return doc;
    }

} // namespace ftxui::ext
