#include "ftxui/ext/latex_math.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ftxui::ext
{

    // Heterogeneous (transparent) hashing for the math engine's static lookup
    // tables. Every one of these is probed once per symbol on every render —
    // during streaming that is every token — and plain unordered_map/set
    // require an exact Key match, forcing a std::string(string_view) copy at
    // each call site. is_transparent opts into C++20 heterogeneous lookup, so
    // .find(string_view) hashes and compares in place.
    struct SvHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    struct SvEq
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const { return a == b; }
    };
    template <typename V>
    using SvMap = std::unordered_map<std::string, V, SvHash, SvEq>;
    using SvSet = std::unordered_set<std::string, SvHash, SvEq>;

    // -------------------------------------------------------------------
    // LaTeX bracket delimiters -> dollar delimiters (pre-parse normalization)
    // -------------------------------------------------------------------

    std::optional<std::pair<size_t, size_t>> find_math_span(std::string_view text, size_t from);

    namespace
    {

        // A span is math if it carries a TeX control sequence or sub/superscripts.
        // Plain bracketed prose does not, which keeps "\[see below\]" as literal text.
        bool looks_like_math(std::string_view span)
        {
            for (size_t i = 0; i < span.size(); ++i)
            {
                if (span[i] == '^' || span[i] == '_')
                    return true;
                if (span[i] == '\\' && i + 1 < span.size() &&
                    std::isalpha(static_cast<unsigned char>(span[i + 1])))
                    return true;
            }
            return false;
        }

        // Length of the backtick run at `pos`, or 0.
        size_t backtick_run(std::string_view text, size_t pos)
        {
            size_t n = 0;
            while (pos + n < text.size() && text[pos + n] == '`')
                ++n;
            return n;
        }

        // Markdown parses the inside of an equation as prose unless it is escaped, and
        // it does real damage: the _ pairs in S_{\text{Total}} ... \mathcal{L}_M become
        // emphasis delimiters, which both delete the underscores and split the equation
        // across inline nodes so its $$ can no longer be matched. Backticks would open
        // code spans, and \, is read as an escaped comma and loses its backslash.
        //
        // A backslash escape of any ASCII punctuation yields that character literally,
        // so escaping every punctuation character makes markdown return the span
        // byte-for-byte.
        std::string escape_math_content(std::string_view span)
        {
            std::string out;
            out.reserve(span.size() * 2);
            for (char c : span)
            {
                if (std::ispunct(static_cast<unsigned char>(c)))
                    out += '\\';
                out += c;
            }
            return out;
        }

    } // namespace

    // Defined with the environment machinery further down.
    bool is_known_math_environment(std::string_view name);
    size_t find_env_end(std::string_view input, size_t pos,
                        std::string_view env_name, size_t &after_end);

    std::string NormalizeMathDelimiters(std::string_view source)
    {
        std::string out;
        out.reserve(source.size());

        bool at_line_start = true;
        bool in_fence = false;
        size_t i = 0;
        while (i < source.size())
        {
            const char c = source[i];

            // Fenced code blocks: copy verbatim between fences.
            if (at_line_start && (c == '`' || c == '~'))
            {
                const size_t run = backtick_run(source, i);
                const size_t tilde = (c == '~') ? [&]
                {
                    size_t n = 0;
                    while (i + n < source.size() && source[i + n] == '~')
                        ++n;
                    return n;
                }()
                                                : 0;
                if (run >= 3 || tilde >= 3)
                {
                    in_fence = !in_fence;
                    const size_t width = run >= 3 ? run : tilde;
                    out.append(source.substr(i, width));
                    i += width;
                    at_line_start = false;
                    continue;
                }
            }
            if (in_fence)
            {
                out += c;
                at_line_start = (c == '\n');
                ++i;
                continue;
            }

            // Inline code span: copy through the matching backtick run.
            if (c == '`')
            {
                const size_t run = backtick_run(source, i);
                const size_t close = source.find(std::string(run, '`'), i + run);
                const size_t end = close == std::string_view::npos
                                       ? source.size()
                                       : close + run;
                out.append(source.substr(i, end - i));
                i = end;
                at_line_start = false;
                continue;
            }

            // A bare top-level \begin{env}...\end{env} with no $$ around it is
            // a display equation for every environment the renderer can lay
            // out — LLMs emit these constantly. Wrap and escape it like any
            // other math span so markdown returns it byte-for-byte.
            if (c == '\\' && source.compare(i, 7, "\\begin{") == 0)
            {
                const size_t name_start = i + 7;
                const size_t name_end = source.find('}', name_start);
                if (name_end != std::string_view::npos &&
                    is_known_math_environment(
                        source.substr(name_start, name_end - name_start)))
                {
                    const std::string_view env_name =
                        source.substr(name_start, name_end - name_start);
                    size_t after_end = 0;
                    if (find_env_end(source, name_end + 1, env_name, after_end) !=
                        std::string_view::npos)
                    {
                        out.append("$$");
                        out += escape_math_content(source.substr(i, after_end - i));
                        out.append("$$");
                        i = after_end;
                        at_line_start = false;
                        continue;
                    }
                }
            }

            if (c == '\\' && i + 1 < source.size() &&
                (source[i + 1] == '(' || source[i + 1] == '['))
            {
                const bool display = source[i + 1] == '[';
                const std::string_view closer = display ? "\\]" : "\\)";
                const size_t body = i + 2;
                const size_t close = source.find(closer, body);
                if (close != std::string_view::npos &&
                    looks_like_math(source.substr(body, close - body)))
                {
                    const std::string_view fence = display ? "$$" : "$";
                    out.append(fence);
                    out += escape_math_content(source.substr(body, close - body));
                    out.append(fence);
                    i = close + closer.size();
                    at_line_start = false;
                    continue;
                }
            }

            // Dollar-delimited math needs the same protection; only its innards are
            // escaped, so the delimiters still read as delimiters afterwards.
            if (c == '$')
            {
                if (auto span = find_math_span(source, i); span && span->first == i)
                {
                    const size_t delim = (i + 1 < source.size() && source[i + 1] == '$') ? 2 : 1;
                    const std::string_view fence = source.substr(i, delim);
                    const size_t body = i + delim;
                    const size_t body_end = span->second - delim;
                    out.append(fence);
                    out += escape_math_content(source.substr(body, body_end - body));
                    out.append(fence);
                    i = span->second;
                    at_line_start = false;
                    continue;
                }
            }

            out += c;
            at_line_start = (c == '\n');
            ++i;
        }
        return out;
    }

    // -------------------------------------------------------------------
    // Small terminal math renderer
    // -------------------------------------------------------------------

    struct LayoutBox
    {
        std::vector<std::string> rows;
        int height_above = 0;
        int height_below = 0;

        int width() const
        {
            int result = 0;
            for (const auto &row : rows)
                result = std::max(result, ftxui::string_width(row));
            return result;
        }
        int total_height() const { return height_above + height_below + 1; }
    };

    LayoutBox math_text(std::string text)
    {
        if (text.empty())
            return {{{""}}, 0, 0};
        return {{{std::move(text)}}, 0, 0};
    }

    // A run of `count` copies of a box-drawing glyph — fraction bars and radical
    // overlines are drawn as text so they land on the character grid.
    std::string rule(int count, std::string_view glyph = "─")
    {
        std::string out;
        out.reserve(static_cast<size_t>(std::max(0, count)) * glyph.size());
        for (int i = 0; i < std::max(0, count); ++i)
            out += glyph;
        return out;
    }

    std::string spaces(int count)
    {
        return std::string(static_cast<size_t>(std::max(0, count)), ' ');
    }

    // Enforce the LayoutBox invariant rows.size() == height_above + 1 +
    // height_below so composition — which indexes rows by the height fields —
    // can never silently skip or overhang content. Producers maintain this by
    // construction; this is a defensive net for nested sub-expressions. The
    // baseline (height_above) is the source of truth: when the row count
    // exceeds the declared height, height_below grows to fit instead of the
    // excess rows being dropped.
    void normalize_box(LayoutBox &box)
    {
        const int have = static_cast<int>(box.rows.size());
        const int declared = box.height_above + 1 + box.height_below;
        if (have == declared)
            return;
        if (have < declared)
            box.rows.resize(static_cast<size_t>(declared));
        else
            box.height_below = have - 1 - box.height_above;
    }

    bool box_is_normalized(const LayoutBox &box)
    {
        return static_cast<int>(box.rows.size()) ==
               box.height_above + 1 + box.height_below;
    }

    LayoutBox math_hbox(const LayoutBox &left, const LayoutBox &right)
    {
        LayoutBox left_copy, right_copy;
        const LayoutBox *l = &left;
        const LayoutBox *r = &right;
        if (!box_is_normalized(left))
        {
            left_copy = left;
            normalize_box(left_copy);
            l = &left_copy;
        }
        if (!box_is_normalized(right))
        {
            right_copy = right;
            normalize_box(right_copy);
            r = &right_copy;
        }
        const int top = std::max(l->height_above, r->height_above);
        const int bottom = std::max(l->height_below, r->height_below);
        const int height = top + 1 + bottom;
        const int left_width = l->width();

        LayoutBox result;
        result.height_above = top;
        result.height_below = bottom;
        result.rows.resize(static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            const int left_y = y - (top - l->height_above);
            const int right_y = y - (top - r->height_above);
            std::string left_row = left_y >= 0 && left_y < static_cast<int>(l->rows.size())
                                       ? l->rows[static_cast<size_t>(left_y)]
                                       : "";
            std::string right_row = right_y >= 0 && right_y < static_cast<int>(r->rows.size())
                                        ? r->rows[static_cast<size_t>(right_y)]
                                        : "";
            left_row += spaces(left_width - ftxui::string_width(left_row));
            result.rows[static_cast<size_t>(y)] = std::move(left_row) + std::move(right_row);
        }

        return result;
    }

    // Combine a whole row of boxes in one pass.
    //
    // Folding these pairwise with math_hbox is quadratic: each combine copies the
    // entire accumulated row set and recalls width(), which UTF-8-decodes every row
    // it has already decoded. That cost compounds during streaming, where the
    // in-flight reply is re-rendered on every frame as the equation grows.
    //
    // Here each piece's bytes are appended exactly once, and each row's width is
    // carried forward rather than re-measured, so the work is linear in the output.
    LayoutBox math_hrow(const std::vector<LayoutBox> &pieces)
    {
        std::vector<LayoutBox> normalized;
        const std::vector<LayoutBox> *use = &pieces;
        for (const auto &piece : pieces)
            if (!box_is_normalized(piece))
            {
                normalized = pieces;
                for (auto &piece : normalized)
                    normalize_box(piece);
                use = &normalized;
                break;
            }
        int top = 0, bottom = 0;
        for (const auto &piece : *use)
        {
            top = std::max(top, piece.height_above);
            bottom = std::max(bottom, piece.height_below);
        }
        const int height = top + 1 + bottom;

        LayoutBox result;
        result.height_above = top;
        result.height_below = bottom;
        result.rows.assign(static_cast<size_t>(height), std::string());

        std::vector<int> row_width(static_cast<size_t>(height), 0);
        int placed_width = 0;
        for (const auto &piece : *use)
        {
            const int piece_width = piece.width();
            const int row_offset = top - piece.height_above;
            for (int y = 0; y < height; ++y)
            {
                const int piece_y = y - row_offset;
                if (piece_y < 0 || piece_y >= static_cast<int>(piece.rows.size()))
                    continue;
                const std::string &source = piece.rows[static_cast<size_t>(piece_y)];
                if (source.empty())
                    continue;
                auto &row = result.rows[static_cast<size_t>(y)];
                // Left-pad only when this row actually receives content, so rows
                // that stay empty never accumulate trailing blanks.
                row += spaces(placed_width - row_width[static_cast<size_t>(y)]);
                row += source;
                row_width[static_cast<size_t>(y)] =
                    placed_width + ftxui::string_width(source);
            }
            placed_width += piece_width;
        }
        return result;
    }

    std::string next_glyph(std::string_view input, size_t &pos);

    // Flatten a box to one line. Inline-mode boxes are single-row by construction;
    // the join is a safety net so a stray multi-row box degrades to a readable
    // line rather than corrupting the row count of its container.
    std::string flatten_box(const LayoutBox &box)
    {
        std::string result;
        for (const auto &row : box.rows)
        {
            if (!result.empty() && !result.ends_with(' ') && !row.empty())
                result += ' ';
            result += row;
        }
        return result;
    }

    std::string_view trim_spaces_view(std::string_view text)
    {
        const size_t begin = text.find_first_not_of(' ');
        if (begin == std::string_view::npos)
            return {};
        return text.substr(begin, text.find_last_not_of(' ') - begin + 1);
    }

    std::string trim_spaces(std::string_view text)
    {
        return std::string(trim_spaces_view(text));
    }

    // An operand needs bracketing in "a/b" notation unless it is a single atom;
    // without it "a+b"/"c" would read as "a+b/c".
    bool needs_parens(std::string_view operand)
    {
        if (operand.empty())
            return false;
        if (operand.find_first_of(" +-*/=<>^_") != std::string_view::npos)
            return true;
        size_t pos = 0, glyphs = 0;
        while (pos < operand.size())
        {
            next_glyph(operand, pos);
            if (++glyphs > 1)
                return true;
        }
        return false;
    }

    // Single-row "a/b", preferring a precomposed vulgar fraction when one exists.
    LayoutBox linear_fraction(const LayoutBox &numerator,
                              const LayoutBox &denominator)
    {
        static const SvMap<std::string> vulgar = {
            {"1/2", "½"},
            {"1/3", "⅓"},
            {"2/3", "⅔"},
            {"1/4", "¼"},
            {"3/4", "¾"},
            {"1/5", "⅕"},
            {"2/5", "⅖"},
            {"3/5", "⅗"},
            {"4/5", "⅘"},
            {"1/6", "⅙"},
            {"5/6", "⅚"},
            {"1/8", "⅛"},
            {"3/8", "⅜"},
            {"5/8", "⅝"},
            {"7/8", "⅞"},
        };
        const std::string top = trim_spaces(flatten_box(numerator));
        const std::string bottom = trim_spaces(flatten_box(denominator));
        // Every vulgar-fraction key is a single ASCII digit on each side, so
        // skip building the "n/d" probe key entirely for the common case of
        // multi-character operands — it can never match.
        if (top.size() == 1 && bottom.size() == 1)
        {
            char key[3] = {top[0], '/', bottom[0]};
            if (auto it = vulgar.find(std::string_view(key, 3)); it != vulgar.end())
                return math_text(it->second);
        }
        const std::string left = needs_parens(top) ? "(" + top + ")" : top;
        const std::string right = needs_parens(bottom) ? "(" + bottom + ")" : bottom;
        return math_text(left + "/" + right);
    }

    LayoutBox math_fraction(const LayoutBox &numerator,
                            const LayoutBox &denominator,
                            MathMode mode)
    {
        if (mode == MathMode::Inline)
            return linear_fraction(numerator, denominator);
        const int content_width = std::max(numerator.width(), denominator.width());
        const int width = content_width + 2;
        LayoutBox result;
        result.height_above = static_cast<int>(numerator.rows.size());
        result.height_below = static_cast<int>(denominator.rows.size());
        result.rows.reserve(numerator.rows.size() + denominator.rows.size() + 1);

        auto add_centered = [&](const LayoutBox &part)
        {
            const int padding = (width - part.width()) / 2;
            for (const auto &row : part.rows)
                result.rows.push_back(spaces(padding) + row);
        };
        add_centered(numerator);
        result.rows.push_back(rule(std::max(1, width)));
        add_centered(denominator);

        return result;
    }

    LayoutBox math_integral(int height, std::string_view symbol = "int")
    {
        LayoutBox result;
        const std::string single_sym = symbol == "oint" ? "∮" : symbol == "iint" ? "∬"
                                                            : symbol == "iiint"  ? "∭"
                                                                                 : "∫";
        if (height <= 1)
        {
            return math_text(single_sym);
        }
        result.height_above = height / 2;
        result.height_below = height - 1 - result.height_above;
        result.rows.reserve(static_cast<size_t>(height));
        result.rows.push_back("⌠");
        for (int i = 1; i < height - 1; ++i)
            result.rows.push_back("│");
        result.rows.push_back("⌡");
        return result;
    }

    std::string next_glyph(std::string_view input, size_t &pos)
    {
        const size_t start = pos++;
        if (static_cast<unsigned char>(input[start]) < 0x80)
            return std::string(input.substr(start, 1));
        while (pos < input.size() &&
               (static_cast<unsigned char>(input[pos]) & 0xC0) == 0x80)
            ++pos;
        return std::string(input.substr(start, pos - start));
    }

    // `atomic` marks a target that came from a single control sequence (\mu, \gamma):
    // it is one symbol, so it is looked up whole and never decomposed into glyphs —
    // otherwise "mu" would be spelled out as the two letters "ᵐᵘ".
    std::string script_glyphs(std::string_view text, bool superscript, bool atomic = false)
    {
        static const SvMap<std::string> super = {
            {"0", "⁰"},
            {"1", "¹"},
            {"2", "²"},
            {"3", "³"},
            {"4", "⁴"},
            {"5", "⁵"},
            {"6", "⁶"},
            {"7", "⁷"},
            {"8", "⁸"},
            {"9", "⁹"},
            {"+", "⁺"},
            {"-", "⁻"},
            {"=", "⁼"},
            {"(", "⁽"},
            {")", "⁾"},
            {".", "·"},
            {"*", "·"},
            {"/", "ᐟ"},
            {"n", "ⁿ"},
            {"i", "ⁱ"},
            {"x", "ˣ"},
            {"y", "ʸ"},
            {"z", "ᶻ"},
            {"a", "ᵃ"},
            {"b", "ᵇ"},
            {"c", "ᶜ"},
            {"d", "ᵈ"},
            {"e", "ᵉ"},
            {"f", "ᶠ"},
            {"g", "ᵍ"},
            {"h", "ʰ"},
            {"k", "ᵏ"},
            {"l", "ˡ"},
            {"m", "ᵐ"},
            {"p", "ᵖ"},
            {"r", "ʳ"},
            {"s", "ˢ"},
            {"t", "ᵗ"},
            {"u", "ᵘ"},
            {"v", "ᵛ"},
            {"w", "ʷ"},
            {"alpha", "ᵅ"},
            {"α", "ᵅ"},
            {"β", "ᵝ"},
            {"gamma", "ᵞ"},
            {"γ", "ᵞ"},
            {"delta", "ᵟ"},
            {"δ", "ᵟ"},
            {"theta", "ᶿ"},
            {"θ", "ᶿ"},
            {"phi", "ᶲ"},
            {"φ", "ᶲ"},
            {"chi", "ᵡ"},
            {"χ", "ᵡ"},
        };
        static const SvMap<std::string> sub = {
            {"0", "₀"},
            {"1", "₁"},
            {"2", "₂"},
            {"3", "₃"},
            {"4", "₄"},
            {"5", "₅"},
            {"6", "₆"},
            {"7", "₇"},
            {"8", "₈"},
            {"9", "₉"},
            {"+", "₊"},
            {"-", "₋"},
            {"=", "₌"},
            {"(", "₍"},
            {")", "₎"},
            {".", "․"},
            {"/", "⸝"},
            {"i", "ᵢ"},
            {"j", "ⱼ"},
            {"n", "ₙ"},
            {"m", "ₘ"},
            {"p", "ₚ"},
            {"r", "ᵣ"},
            {"s", "ₛ"},
            {"t", "ₜ"},
            {"u", "ᵤ"},
            {"v", "ᵥ"},
            {"x", "ₓ"},
            {"a", "ₐ"},
            {"e", "ₑ"},
            {"o", "ₒ"},
            {"h", "ₕ"},
            {"k", "ₖ"},
            {"l", "ₗ"},
            // Unicode has no subscript mu or nu; they fall back to "_μν" rather
            // than borrowing the phi/chi glyphs, which would render g_{\mu\nu}
            // as a different tensor than the one written.
            {"rho", "ᵨ"},
            {"ρ", "ᵨ"},
            {"phi", "ᵩ"},
            {"φ", "ᵩ"},
            {"chi", "ᵪ"},
            {"χ", "ᵪ"},
        };
        const auto &table = superscript ? super : sub;
        if (auto it = table.find(text); it != table.end())
            return it->second;
        if (atomic)
            return (superscript ? "^" : "_") + std::string(text);
        // All-or-nothing: a run is only shifted when every glyph in it has a
        // real superscript/subscript form. Mixing shifted and prefixed glyphs
        // ("^μ^ ^ν") is less readable than one prefix on the whole run. When a
        // run cannot shift whole, it degrades to a parenthetical "^(\u2026)" /
        // "_(\u2026)" — a bare "^"/"_" prefix reads as if it bound only the next
        // token, which loses the rest of the exponent.
        std::string result;
        size_t pos = 0;
        while (pos < text.size())
        {
            auto glyph = next_glyph(text, pos);
            auto it = table.find(glyph);
            if (it == table.end())
                return (superscript ? "^(" : "_(") + std::string(text) + ")";
            result += it->second;
        }
        return result;
    }

    const SvMap<std::string> &math_symbols()
    {
        static const SvMap<std::string> table = {
            {"alpha", "α"},
            {"beta", "β"},
            {"gamma", "γ"},
            {"delta", "δ"},
            {"epsilon", "ϵ"},
            {"varepsilon", "ε"},
            {"zeta", "ζ"},
            {"eta", "η"},
            {"theta", "θ"},
            {"vartheta", "ϑ"},
            {"iota", "ι"},
            {"kappa", "κ"},
            {"lambda", "λ"},
            {"mu", "μ"},
            {"nu", "ν"},
            {"xi", "ξ"},
            {"pi", "π"},
            {"varpi", "ϖ"},
            {"rho", "ρ"},
            {"varrho", "ϱ"},
            {"sigma", "σ"},
            {"varsigma", "ς"},
            {"tau", "τ"},
            {"upsilon", "υ"},
            {"phi", "φ"},
            {"varphi", "ϕ"},
            {"chi", "χ"},
            {"psi", "ψ"},
            {"omega", "ω"},
            {"infty", "∞"},
            {"Alpha", "Α"},
            {"Beta", "Β"},
            {"Gamma", "Γ"},
            {"Delta", "Δ"},
            {"Epsilon", "Ε"},
            {"Zeta", "Ζ"},
            {"Eta", "Η"},
            {"Theta", "Θ"},
            {"Iota", "Ι"},
            {"Kappa", "Κ"},
            {"Lambda", "Λ"},
            {"Mu", "Μ"},
            {"Nu", "Ν"},
            {"Xi", "Ξ"},
            {"Pi", "Π"},
            {"Rho", "Ρ"},
            {"Sigma", "Σ"},
            {"Tau", "Τ"},
            {"Upsilon", "Υ"},
            {"Phi", "Φ"},
            {"Chi", "Χ"},
            {"Psi", "Ψ"},
            {"Omega", "Ω"},
            {"int", "∫"},
            {"sum", "∑"},
            {"prod", "∏"},
            {"coprod", "∐"},
            // Unicode arrows, not ASCII digraphs: "->" bypassed the operator
            // spacing table (it holds "→"), so "A\to B" set tight.
            {"to", "→"},
            {"rightarrow", "→"},
            {"leftarrow", "←"},
            {"gets", "←"},
            {"Rightarrow", "⇒"},
            {"Leftarrow", "⇐"},
            {"leftrightarrow", "↔"},
            {"Leftrightarrow", "⇔"},
            {"longrightarrow", "⟶"},
            {"longleftarrow", "⟵"},
            {"longmapsto", "⟼"},
            {"rightleftharpoons", "⇌"},
            {"rightharpoonup", "⇀"},
            {"leftharpoonup", "↼"},
            {"neq", "≠"},
            {"ne", "≠"},
            {"leq", "≤"},
            {"le", "≤"},
            {"geq", "≥"},
            {"ge", "≥"},
            {"ll", "≪"},
            {"gg", "≫"},
            {"prec", "≺"},
            {"succ", "≻"},
            {"preceq", "⪯"},
            {"succeq", "⪰"},
            {"lt", "<"},
            {"gt", ">"},
            {"mid", "|"},
            {"approx", "≈"},
            {"equiv", "≡"},
            {"sim", "∼"},
            {"simeq", "≃"},
            {"cong", "≅"},
            {"propto", "∝"},
            {"pm", "±"},
            {"mp", "∓"},
            {"times", "×"},
            {"div", "÷"},
            {"cdot", "·"},
            {"cdots", "⋯"},
            // The elision set. \vdots and \ddots complete what \cdots/\ldots started —
            // without them an elided matrix renders its corners and then the literal
            // text "\vdots" down the middle. Names are matched whole, so these do not
            // collide with the \dot/\ddot accents handled further down.
            {"ldots", "…"},
            {"dots", "…"},
            {"vdots", "⋮"},
            {"ddots", "⋱"},
            {"iddots", "⋰"},
            {"partial", "∂"},
            {"nabla", "∇"},
            {"in", "∈"},
            {"notin", "∉"},
            {"ni", "∋"},
            {"subset", "⊂"},
            {"subseteq", "⊆"},
            {"supset", "⊃"},
            {"supseteq", "⊇"},
            {"sqsubseteq", "⊑"},
            {"sqsupseteq", "⊒"},
            {"forall", "∀"},
            {"exists", "∃"},
            {"nexists", "∄"},
            {"nexist", "∄"}, // historical alias; the LaTeX command is \nexists
            {"neg", "¬"},
            {"lnot", "¬"},
            {"top", "⊤"},
            {"bot", "⊥"},
            {"vdash", "⊢"},
            {"dashv", "⊣"},
            {"models", "⊨"},
            {"vDash", "⊨"},
            {"therefore", "∴"},
            {"because", "∵"},
            {"hbar", "ħ"},
            {"hslash", "ℏ"},
            {"ell", "ℓ"},
            {"Re", "ℜ"},
            {"Im", "ℑ"},
            {"wp", "℘"},
            {"aleph", "ℵ"},
            {"beth", "ℶ"},
            {"gimel", "ℷ"},
            {"mathbbR", "ℝ"},
            {"mathbbN", "ℕ"},
            {"mathbbZ", "ℤ"},
            {"mathbbC", "ℂ"},
            {"mathbbQ", "ℚ"},
            {"mathbbH", "ℍ"},
            {"mathbbP", "ℙ"},
            {"cup", "∪"},
            {"cap", "∩"},
            {"sqcup", "⊔"},
            {"sqcap", "⊓"},
            {"bigcup", "⋃"},
            {"bigcap", "⋂"},
            {"bigvee", "⋁"},
            {"bigwedge", "⋀"},
            {"bigoplus", "⨁"},
            {"bigotimes", "⨂"},
            {"bigodot", "⨀"},
            {"bigsqcup", "⨆"},
            {"setminus", "∖"},
            {"emptyset", "∅"},
            {"varnothing", "∅"},
            {"land", "∧"},
            {"wedge", "∧"},
            {"lor", "∨"},
            {"vee", "∨"},
            {"oplus", "⊕"},
            {"otimes", "⊗"},
            {"odot", "⊙"},
            {"ominus", "⊖"},
            {"oslash", "⊘"},
            {"ast", "*"},
            {"star", "⋆"},
            {"dagger", "†"},
            {"ddagger", "‡"},
            {"bullet", "•"},
            {"circ", "∘"},
            {"bigcirc", "◯"},
            {"Uparrow", "⇑"},
            {"Downarrow", "⇓"},
            {"uparrow", "↑"},
            {"downarrow", "↓"},
            {"nearrow", "↗"},
            {"searrow", "↘"},
            {"swarrow", "↙"},
            {"nwarrow", "↖"},
            {"mapsto", "↦"},
            {"hookrightarrow", "↪"},
            {"hookleftarrow", "↩"},
            {"implies", "⟹"},
            {"impliedby", "⟸"},
            {"iff", "⟺"},
            {"parallel", "∥"},
            {"nparallel", "∦"},
            {"perp", "⊥"},
            {"angle", "∠"},
            {"measuredangle", "∡"},
            {"langle", "⟨"},
            {"rangle", "⟩"},
            {"lceil", "⌈"},
            {"rceil", "⌉"},
            {"lfloor", "⌊"},
            {"rfloor", "⌋"},
            {"Vert", "‖"},
            {"triangle", "△"},
            {"triangleleft", "◁"},
            {"triangleright", "▷"},
            {"square", "□"},
            {"blacksquare", "■"},
            {"diamond", "◇"},
            {"prime", "′"},
            {"imath", "ı"},
            {"jmath", "ȷ"},
            {"eth", "ð"},
            {"degree", "°"},
            {"checkmark", "✓"},
            {"sin", "sin"},
            {"cos", "cos"},
            {"tan", "tan"},
            {"csc", "csc"},
            {"sec", "sec"},
            {"cot", "cot"},
            {"sinh", "sinh"},
            {"cosh", "cosh"},
            {"tanh", "tanh"},
            {"coth", "coth"},
            {"sech", "sech"},
            {"csch", "csch"},
            {"arcsin", "arcsin"},
            {"arccos", "arccos"},
            {"arctan", "arctan"},
            {"log", "log"},
            {"lg", "lg"},
            {"ln", "ln"},
            {"exp", "exp"},
            {"lim", "lim"},
            {"limsup", "lim sup"},
            {"liminf", "lim inf"},
            {"max", "max"},
            {"min", "min"},
            {"sup", "sup"},
            {"inf", "inf"},
            {"arg", "arg"},
            {"det", "det"},
            {"dim", "dim"},
            {"ker", "ker"},
            {"hom", "hom"},
            {"deg", "deg"},
            {"gcd", "gcd"},
            {"Pr", "Pr"},
        };
        return table;
    }

    void append_codepoint(std::string &out, uint32_t cp)
    {
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    enum class MathAlphabet
    {
        DoubleStruck, // \mathbb
        Fraktur,      // \mathfrak
        Script,       // \mathcal, \mathscr
    };

    // Map ASCII letters (and digits, for double-struck) onto the Unicode
    // Mathematical Alphanumeric Symbols block (U+1D400..). The older
    // Letterlike Symbols block already held some of these (ℝ, ℋ, ℜ, ...),
    // so those codepoints are holes in the main runs and must be
    // special-cased. Non-ASCII and unmapped characters pass through.
    std::string map_math_alphabet(std::string_view body, MathAlphabet which)
    {
        std::string out;
        out.reserve(body.size() * 4);
        for (char ch : body)
        {
            const unsigned char c = static_cast<unsigned char>(ch);
            uint32_t cp = 0;
            switch (which)
            {
            case MathAlphabet::DoubleStruck:
                switch (c)
                {
                case 'C': cp = 0x2102; break;
                case 'H': cp = 0x210D; break;
                case 'N': cp = 0x2115; break;
                case 'P': cp = 0x2119; break;
                case 'Q': cp = 0x211A; break;
                case 'R': cp = 0x211D; break;
                case 'Z': cp = 0x2124; break;
                default:
                    if (c >= 'A' && c <= 'Z')
                        cp = 0x1D538 + (c - 'A');
                    else if (c >= 'a' && c <= 'z')
                        cp = 0x1D552 + (c - 'a');
                    else if (c >= '0' && c <= '9')
                        cp = 0x1D7D8 + (c - '0');
                    break;
                }
                break;
            case MathAlphabet::Fraktur:
                switch (c)
                {
                case 'C': cp = 0x212D; break;
                case 'H': cp = 0x210C; break;
                case 'I': cp = 0x2111; break;
                case 'R': cp = 0x211C; break;
                case 'Z': cp = 0x2128; break;
                default:
                    if (c >= 'A' && c <= 'Z')
                        cp = 0x1D504 + (c - 'A');
                    else if (c >= 'a' && c <= 'z')
                        cp = 0x1D51E + (c - 'a');
                    break;
                }
                break;
            case MathAlphabet::Script:
                switch (c)
                {
                case 'B': cp = 0x212C; break;
                case 'E': cp = 0x2130; break;
                case 'F': cp = 0x2131; break;
                case 'H': cp = 0x210B; break;
                case 'I': cp = 0x2110; break;
                case 'L': cp = 0x2112; break;
                case 'M': cp = 0x2133; break;
                case 'R': cp = 0x211B; break;
                case 'e': cp = 0x212F; break;
                case 'g': cp = 0x210A; break;
                case 'o': cp = 0x2134; break;
                default:
                    if (c >= 'A' && c <= 'Z')
                        cp = 0x1D49C + (c - 'A');
                    else if (c >= 'a' && c <= 'z')
                        cp = 0x1D4B6 + (c - 'a');
                    break;
                }
                break;
            }
            if (cp)
                append_codepoint(out, cp);
            else
                out += ch;
        }
        return out;
    }

    // Atoms TeX surrounds with space regardless of how the source was typed:
    // relations and binary operators. Everything else — digits, letters, symbols,
    // parentheses, '/' — sets tight against its neighbours.
    //
    // '-' is deliberately included even though it is unary in "-x": the caller
    // suppresses spacing when nothing precedes it, or when what precedes is
    // itself one of these, which covers "= -1" and "(-x)".
    bool is_spaced_operator(std::string_view glyph)
    {
        static const SvSet ops = {
            "=",
            "+",
            "-",
            "<",
            ">",
            "≠",
            "≤",
            "≥",
            "≈",
            "≡",
            "∝",
            "∼",
            "≅",
            "≪",
            "≫",
            "±",
            "∓",
            "×",
            "÷",
            "⋅",
            "∪",
            "∩",
            "⊕",
            "⊗",
            "∈",
            "∉",
            "∋",
            "⊂",
            "⊃",
            "⊆",
            "⊇",
            "≃",
            "≺",
            "≻",
            "⪯",
            "⪰",
            "∧",
            "∨",
            "∖",
            "⊔",
            "⊓",
            "→",
            "←",
            "↔",
            "⇒",
            "⇐",
            "⇔",
            "⟶",
            "⟵",
            "⟹",
            "⟸",
            "⟺",
            "⇌",
            "↦",
            "⊢",
            "⊣",
            "⊨",
            "∴",
            "∵",
            "≔",
        };
        return ops.count(glyph) > 0;
    }

    // Operators that take their limits stacked above and below in display style,
    // rather than set beside them as ordinary scripts.
    bool is_big_operator(std::string_view text)
    {
        static const SvSet ops = {
            "∑",
            "∏",
            "∐",
            "∫",
            "∮",
            "∬",
            "∭",
            "⋃",
            "⋂",
            "⨁",
            "⨂",
            "⨆",
            "lim",
            "max",
            "min",
            "sup",
            "inf",
        };
        return ops.count(trim_spaces_view(text)) > 0;
    }

    // Centres `script` above or below `base`, the way a display-style big operator
    // carries its limits. math_hbox would set them beside it instead, which is
    // correct for ordinary scripts and wrong for these. Multi-row scripts keep
    // their rows (a \frac limit stacks as a fraction, not flattened to a/b);
    // single-row scripts are trimmed so operator spacing inside them doesn't
    // widen the column.
    LayoutBox stack_limit(const LayoutBox &base, const LayoutBox &script, bool above)
    {
        const bool flat_script = script.rows.size() <= 1;
        const std::string flat =
            flat_script ? trim_spaces(flatten_box(script)) : std::string();
        const int script_width =
            flat_script ? ftxui::string_width(flat) : script.width();
        const int script_height =
            flat_script ? 1 : static_cast<int>(script.rows.size());
        const int width = std::max(base.width(), script_width);
        auto centred = [&](const std::string &row)
        {
            const int pad = (width - ftxui::string_width(row)) / 2;
            return spaces(pad) + row +
                   spaces(width - pad - ftxui::string_width(row));
        };
        auto add_script = [&](LayoutBox &out)
        {
            if (flat_script)
                out.rows.push_back(centred(flat));
            else
                for (const auto &row : script.rows)
                    out.rows.push_back(centred(row));
        };
        LayoutBox out;
        out.height_above = base.height_above + (above ? script_height : 0);
        out.height_below = base.height_below + (above ? 0 : script_height);
        if (above)
            add_script(out);
        for (const auto &row : base.rows)
            out.rows.push_back(centred(row));
        if (!above)
            add_script(out);
        return out;
    }

    // A delimiter stretched over `height_above + 1 + height_below` rows, as a
    // one-column LayoutBox. Height 1 falls back to the plain glyph; "." is the
    // null delimiter (\left. / \right.) and yields a zero-width column; glyphs
    // with no extensible bracket-piece form sit alone on the baseline row.
    LayoutBox make_delimiter_column(std::string_view delim, int height_above, int height_below)
    {
        const int height = height_above + 1 + height_below;
        LayoutBox out;
        out.height_above = height_above;
        out.height_below = height_below;
        if (delim.empty() || delim == ".")
        {
            out.rows.assign(static_cast<size_t>(height), "");
            return out;
        }
        if (height <= 1)
        {
            out.rows.push_back(std::string(delim));
            return out;
        }
        struct Ext
        {
            const char *top, *mid, *bottom;
            const char *hook; // curly braces: the pointed middle piece, on the baseline
        };
        Ext ext{};
        if (delim == "(")
            ext = {"⎛", "⎜", "⎝", nullptr};
        else if (delim == ")")
            ext = {"⎞", "⎟", "⎠", nullptr};
        else if (delim == "[")
            ext = {"⎡", "⎢", "⎣", nullptr};
        else if (delim == "]")
            ext = {"⎤", "⎥", "⎦", nullptr};
        else if (delim == "{")
            ext = {"⎧", "⎪", "⎩", "⎨"};
        else if (delim == "}")
            ext = {"⎫", "⎪", "⎭", "⎬"};
        else if (delim == "|")
            ext = {"│", "│", "│", nullptr};
        else if (delim == "‖")
            ext = {"║", "║", "║", nullptr};
        else if (delim == "⌈")
            ext = {"⌈", "⎢", "⎢", nullptr};
        else if (delim == "⌉")
            ext = {"⌉", "⎥", "⎥", nullptr};
        else if (delim == "⌊")
            ext = {"⎢", "⎢", "⌊", nullptr};
        else if (delim == "⌋")
            ext = {"⎥", "⎥", "⌋", nullptr};
        else
        {
            // ⟨, ⟩, /, … have no bracket pieces — baseline glyph, blank elsewhere.
            out.rows.assign(static_cast<size_t>(height), " ");
            out.rows[static_cast<size_t>(height_above)] = std::string(delim);
            return out;
        }
        out.rows.reserve(static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            if (y == 0)
                out.rows.push_back(ext.top);
            else if (y == height - 1)
                out.rows.push_back(ext.bottom);
            else if (ext.hook && y == height_above)
                out.rows.push_back(ext.hook);
            else
                out.rows.push_back(ext.mid);
        }
        return out;
    }

    // Stack line boxes vertically; the baseline lands on the middle row so the
    // stack centres against neighbouring content. `centre` pads every row to
    // the common width symmetrically (matrix columns, \binom, \substack);
    // without it lines stay left-aligned (\\ line breaks).
    LayoutBox vstack_boxes(const std::vector<LayoutBox> &lines, bool centre)
    {
        int width = 0;
        int total = 0;
        for (const auto &line : lines)
        {
            width = std::max(width, line.width());
            total += line.total_height();
        }
        LayoutBox out;
        if (total == 0)
            return math_text("");
        out.height_above = total / 2;
        out.height_below = total - 1 - out.height_above;
        out.rows.reserve(static_cast<size_t>(total));
        for (const auto &line : lines)
            for (const auto &row : line.rows)
            {
                if (!centre)
                {
                    out.rows.push_back(row);
                    continue;
                }
                const int pad = width - ftxui::string_width(row);
                const int left = pad / 2;
                out.rows.push_back(spaces(left) + row + spaces(pad - left));
            }
        return out;
    }

    // The delimiter token after \left, \right, or \middle: a single character
    // ("(", ".", "|"), an escaped punct ("\{", "\|"), or a named delimiter
    // macro ("\langle", "\lfloor"). Returns "." (the null delimiter) when
    // nothing usable follows.
    std::string read_delim_token(std::string_view input, size_t &pos)
    {
        while (pos < input.size() &&
               std::isspace(static_cast<unsigned char>(input[pos])))
            ++pos;
        if (pos >= input.size())
            return ".";
        const char c = input[pos];
        if (c != '\\')
        {
            ++pos;
            return std::string(1, c);
        }
        ++pos;
        if (pos < input.size() &&
            !std::isalpha(static_cast<unsigned char>(input[pos])))
        {
            const char punct = input[pos++];
            return punct == '|' ? std::string("‖") : std::string(1, punct);
        }
        const size_t start = pos;
        while (pos < input.size() &&
               std::isalpha(static_cast<unsigned char>(input[pos])))
            ++pos;
        const std::string name(input.substr(start, pos - start));
        static const SvMap<std::string> named = {
            {"langle", "⟨"}, {"rangle", "⟩"}, {"lceil", "⌈"}, {"rceil", "⌉"},
            {"lfloor", "⌊"}, {"rfloor", "⌋"}, {"vert", "|"}, {"Vert", "‖"},
            {"lbrace", "{"}, {"rbrace", "}"}, {"lbrack", "["}, {"rbrack", "]"},
            {"backslash", "\\"},
        };
        if (auto it = named.find(name); it != named.end())
            return it->second;
        return ".";
    }

    // The raw text of a { ... } group, brace-balanced, without parsing it.
    // Constructs whose body must be split before layout (\substack) need this.
    std::optional<std::string_view> read_brace_group_raw(std::string_view input, size_t &pos)
    {
        if (pos >= input.size() || input[pos] != '{')
            return std::nullopt;
        size_t depth = 0;
        const size_t start = pos + 1;
        for (size_t i = pos; i < input.size(); ++i)
        {
            if (input[i] == '\\')
            {
                ++i; // escaped character never opens/closes a group
                continue;
            }
            if (input[i] == '{')
                ++depth;
            else if (input[i] == '}')
            {
                if (--depth == 0)
                {
                    std::string_view body = input.substr(start, i - start);
                    pos = i + 1;
                    return body;
                }
            }
        }
        return std::nullopt;
    }

    // `text_mode` is set only for \text/\mathrm-family bodies, whose content is
    // prose rather than maths: it keeps source whitespace, which math mode
    // deliberately discards (see the whitespace branch in parse_math_sequence).
    //
    // `right_delim_out` non-null marks a sub-parse spawned by \left: the first
    // \right at this level ends the sequence and its delimiter token is written
    // there. `allow_linebreaks` is set only for the top-level display-math
    // sequence, where \\ starts a new stacked line; groups and environments
    // keep their \\ semantics.
    std::optional<LayoutBox> parse_math_group(std::string_view input, size_t &pos, MathMode mode,
                                              bool text_mode = false);
    LayoutBox parse_math_sequence(std::string_view input, size_t &pos, char stop, MathMode mode,
                                  bool text_mode = false,
                                  std::string *right_delim_out = nullptr,
                                  bool allow_linebreaks = false);

    // Split `text` on top-level occurrences of `delim` ('&', or '\\' meaning
    // the \\ row separator), ignoring occurrences nested inside braces or
    // inner environments — a cases block inside an align cell keeps its own
    // rows.
    std::vector<std::string_view> split_top_level(std::string_view text, char delim)
    {
        std::vector<std::string_view> parts;
        int brace_depth = 0;
        int env_depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char ch = text[i];
            if (ch == '\\')
            {
                if (i + 1 < text.size() && text[i + 1] == '\\')
                {
                    if (delim == '\\' && brace_depth == 0 && env_depth == 0)
                    {
                        parts.push_back(text.substr(start, i - start));
                        start = i + 2;
                    }
                    ++i;
                    continue;
                }
                size_t j = i + 1;
                while (j < text.size() &&
                       std::isalpha(static_cast<unsigned char>(text[j])))
                    ++j;
                const std::string_view macro = text.substr(i + 1, j - i - 1);
                if (macro == "begin")
                    ++env_depth;
                else if (macro == "end")
                    env_depth = std::max(0, env_depth - 1);
                // Skip the macro name, or the single escaped character.
                i = (j > i + 1) ? j - 1 : i + 1;
                continue;
            }
            if (ch == '{')
                ++brace_depth;
            else if (ch == '}')
                brace_depth = std::max(0, brace_depth - 1);
            else if (ch == '&' && delim == '&' && brace_depth == 0 && env_depth == 0)
            {
                parts.push_back(text.substr(start, i - start));
                start = i + 1;
            }
        }
        parts.push_back(text.substr(start));
        return parts;
    }

    // Start of the matching \end{name} for a body beginning at `pos` (just
    // past \begin{name}), skipping nested same-name environments.
    // `after_end` receives the index one past the \end tag.
    size_t find_env_end(std::string_view input, size_t pos,
                        std::string_view env_name, size_t &after_end)
    {
        const std::string begin_tag = "\\begin{" + std::string(env_name) + "}";
        const std::string end_tag = "\\end{" + std::string(env_name) + "}";
        int depth = 0;
        size_t i = pos;
        while (i < input.size())
        {
            const size_t b = input.find(begin_tag, i);
            const size_t e = input.find(end_tag, i);
            if (e == std::string_view::npos)
                return std::string_view::npos;
            if (b != std::string_view::npos && b < e)
            {
                ++depth;
                i = b + begin_tag.size();
                continue;
            }
            if (depth == 0)
            {
                after_end = e + end_tag.size();
                return e;
            }
            --depth;
            i = e + end_tag.size();
        }
        return std::string_view::npos;
    }

    // How an environment's & columns are horizontally aligned.
    enum class ColumnStyle
    {
        Centred,    // matrices
        AlignPairs, // align family: even columns right-aligned, odd left —
                    // the relation lands in the same terminal column each row
        LeftAll,    // cases
        Spec,       // array{lcr}
    };

    struct EnvTraits
    {
        std::string_view left_delim = ".";
        std::string_view right_delim = ".";
        ColumnStyle columns = ColumnStyle::Centred;
        bool wide_gap = false; // cases: value vs condition
    };

    // Traits for a known environment; `name` arrives star-stripped.
    std::optional<EnvTraits> env_traits(std::string_view name)
    {
        if (name == "matrix" || name == "smallmatrix")
            return EnvTraits{};
        if (name == "pmatrix")
            return EnvTraits{"(", ")", ColumnStyle::Centred, false};
        if (name == "bmatrix")
            return EnvTraits{"[", "]", ColumnStyle::Centred, false};
        if (name == "Bmatrix")
            return EnvTraits{"{", "}", ColumnStyle::Centred, false};
        if (name == "vmatrix")
            return EnvTraits{"|", "|", ColumnStyle::Centred, false};
        if (name == "Vmatrix")
            return EnvTraits{"‖", "‖", ColumnStyle::Centred, false};
        if (name == "cases")
            return EnvTraits{"{", ".", ColumnStyle::LeftAll, true};
        if (name == "aligned" || name == "align" || name == "split" ||
            name == "alignat" || name == "eqnarray" || name == "flalign")
            return EnvTraits{".", ".", ColumnStyle::AlignPairs, false};
        if (name == "gather" || name == "gathered")
            return EnvTraits{};
        if (name == "array")
            return EnvTraits{".", ".", ColumnStyle::Spec, false};
        return std::nullopt;
    }

    bool is_known_math_environment(std::string_view name)
    {
        std::string base(name);
        if (!base.empty() && base.back() == '*')
            base.pop_back();
        // equation/displaymath have no & structure; the parser drops their
        // tags and renders the body, which is exactly right.
        return env_traits(base).has_value() || base == "equation" ||
               base == "displaymath";
    }

    std::optional<LayoutBox> parse_math_matrix(std::string_view input, size_t &pos,
                                               std::string_view env_name,
                                               const EnvTraits &traits,
                                               std::string_view colspec, MathMode mode)
    {
        size_t after_end = 0;
        const size_t end_pos = find_env_end(input, pos, env_name, after_end);
        if (end_pos == std::string_view::npos)
            return std::nullopt;

        std::string_view body = input.substr(pos, end_pos - pos);
        pos = after_end;

        // Alignment letters only — the | separators of an array colspec have
        // no terminal rendering worth the columns they cost.
        std::string align_spec;
        for (char sc : colspec)
            if (sc == 'l' || sc == 'c' || sc == 'r')
                align_spec += sc;

        // In the align family the odd cells usually open with a relation
        // ("x &= y"): a leading {} gives the operator-spacing rule a
        // preceding atom, so the = spaces as " = " exactly like TeX's {}=.
        const bool pad_odd_cells = traits.columns == ColumnStyle::AlignPairs;

        std::vector<std::vector<std::string>> raw_grid;
        for (std::string_view raw_row : split_top_level(body, '\\'))
        {
            std::vector<std::string> raw_cells;
            size_t cell_index = 0;
            for (std::string_view raw_cell : split_top_level(raw_row, '&'))
            {
                if (pad_odd_cells && (cell_index % 2) == 1)
                    raw_cells.push_back("{}" + std::string(raw_cell));
                else
                    raw_cells.push_back(std::string(raw_cell));
                ++cell_index;
            }
            // A row that is pure whitespace (trailing \\ before \end) is not a row.
            const bool empty_row =
                raw_cells.size() == 1 &&
                raw_cells[0].find_first_not_of(" \t\r\n") == std::string::npos;
            if (!empty_row)
                raw_grid.push_back(std::move(raw_cells));
        }

        if (raw_grid.empty())
            return math_text("");

        if (mode == MathMode::Inline)
        {
            // One-row fallback: rows joined by "; " inside the environment's
            // own delimiters — "(a b; c d)", "{x²; -x", "x = y; y = z".
            std::string line;
            if (traits.left_delim != ".")
                line += traits.left_delim;
            for (size_t r = 0; r < raw_grid.size(); ++r)
            {
                if (r > 0)
                    line += "; ";
                for (size_t c = 0; c < raw_grid[r].size(); ++c)
                {
                    size_t cpos = 0;
                    auto box = parse_math_sequence(raw_grid[r][c], cpos, '\0', mode);
                    const std::string cell = trim_spaces(flatten_box(box));
                    if (cell.empty())
                        continue;
                    if (!line.empty() && line.back() != ' ' &&
                        line != traits.left_delim)
                        line += " ";
                    line += cell;
                }
            }
            if (traits.right_delim != ".")
                line += traits.right_delim;
            return math_text(line);
        }

        size_t nrows = raw_grid.size();
        size_t ncols = 0;
        for (const auto &row : raw_grid)
            ncols = std::max(ncols, row.size());
        if (ncols == 0)
            return math_text("");

        std::vector<std::vector<LayoutBox>> grid(nrows);
        std::vector<int> col_widths(ncols, 0);
        std::vector<int> row_heights_above(nrows, 0);
        std::vector<int> row_heights_below(nrows, 0);

        for (size_t r = 0; r < nrows; ++r)
        {
            grid[r].reserve(ncols);
            for (size_t c = 0; c < ncols; ++c)
            {
                if (c < raw_grid[r].size())
                {
                    size_t cpos = 0;
                    auto box = parse_math_sequence(raw_grid[r][c], cpos, '\0', mode);
                    col_widths[c] = std::max(col_widths[c], box.width());
                    row_heights_above[r] = std::max(row_heights_above[r], box.height_above);
                    row_heights_below[r] = std::max(row_heights_below[r], box.height_below);
                    grid[r].push_back(std::move(box));
                }
                else
                {
                    grid[r].push_back(math_text(""));
                }
            }
        }

        std::vector<LayoutBox> row_boxes;
        row_boxes.reserve(nrows);
        for (size_t r = 0; r < nrows; ++r)
        {
            std::vector<LayoutBox> aligned_cells;
            aligned_cells.reserve(ncols * 2);
            for (size_t c = 0; c < ncols; ++c)
            {
                if (c > 0)
                {
                    // Align pairs join at the & (the {} prefix already spaces
                    // the relation); everything else gets a column gap, wide
                    // for cases where it separates value from condition.
                    if (traits.columns == ColumnStyle::AlignPairs)
                        aligned_cells.push_back(
                            math_text((c % 2) == 1 ? "" : "    "));
                    else
                        aligned_cells.push_back(
                            math_text(traits.wide_gap ? "   " : "  "));
                }
                LayoutBox cell = std::move(grid[r][c]);
                const int pad = col_widths[c] - cell.width();
                int left_pad = pad / 2;
                switch (traits.columns)
                {
                case ColumnStyle::Centred:
                    break;
                case ColumnStyle::LeftAll:
                    left_pad = 0;
                    break;
                case ColumnStyle::AlignPairs:
                    // Right-align the lhs so relations line up at the &.
                    left_pad = (c % 2) == 0 ? pad : 0;
                    break;
                case ColumnStyle::Spec:
                {
                    const char al = c < align_spec.size() ? align_spec[c] : 'c';
                    left_pad = al == 'l' ? 0 : al == 'r' ? pad : pad / 2;
                    break;
                }
                }
                const int right_pad = pad - left_pad;
                if (left_pad > 0 || right_pad > 0)
                {
                    // In-place per-row prepend/append. math_hbox(padbox, cell)
                    // pads every row to a constant left_width regardless of
                    // that row's own content — a padding box is single-row
                    // with height_above/below 0, so every other row of cell
                    // sees an empty left_row and gets the same spaces(pad)
                    // prefix width would have given it. A direct per-row
                    // splice is equivalent and one string edit instead of
                    // two full box rebuilds.
                    const std::string lpad = spaces(left_pad);
                    const std::string rpad = spaces(right_pad);
                    for (auto &row : cell.rows)
                        row = lpad + row + rpad;
                }
                // Grow the cell to the row's full height with blank rows.
                // Claiming the height without the rows would misplace short
                // cells: math_hrow maps piece row 0 to the claimed top, so a
                // one-row cell next to a fraction floated to the top of the
                // row instead of sitting on the shared baseline.
                const int add_above = row_heights_above[r] - cell.height_above;
                const int add_below = row_heights_below[r] - cell.height_below;
                if (add_above > 0)
                    cell.rows.insert(cell.rows.begin(),
                                     static_cast<size_t>(add_above), std::string());
                if (add_below > 0)
                    cell.rows.insert(cell.rows.end(),
                                     static_cast<size_t>(add_below), std::string());
                cell.height_above = row_heights_above[r];
                cell.height_below = row_heights_below[r];
                aligned_cells.push_back(std::move(cell));
            }
            row_boxes.push_back(math_hrow(aligned_cells));
        }

        int total_height = 0;
        for (const auto &rb : row_boxes)
            total_height += rb.total_height();

        LayoutBox matrix_body;
        matrix_body.height_above = total_height / 2;
        matrix_body.height_below = total_height - 1 - matrix_body.height_above;
        matrix_body.rows.reserve(static_cast<size_t>(total_height));

        for (size_t r = 0; r < nrows; ++r)
        {
            for (const auto &row_str : row_boxes[r].rows)
            {
                matrix_body.rows.push_back(row_str);
            }
        }

        if (traits.left_delim == "." && traits.right_delim == ".")
            return matrix_body;

        LayoutBox left_box = make_delimiter_column(
            traits.left_delim, matrix_body.height_above, matrix_body.height_below);
        LayoutBox right_box = make_delimiter_column(
            traits.right_delim, matrix_body.height_above, matrix_body.height_below);
        return math_hrow({std::move(left_box), std::move(matrix_body),
                          std::move(right_box)});
    }

    LayoutBox parse_math_sequence(std::string_view input, size_t &pos, char stop, MathMode mode,
                                  bool text_mode,
                                  std::string *right_delim_out,
                                  bool allow_linebreaks)
    {
        std::vector<LayoutBox> pieces;
        // Lines completed by \\ when allow_linebreaks is set; stacked at the end.
        std::vector<LayoutBox> completed_lines;
        // Tracks whether the last atom emitted was itself a spaced operator, so a
        // sign directly after one ("= -1") reads as unary and stays tight.
        bool prev_was_operator = false;
        // Whether pieces.back() is a big operator (possibly one that already
        // carries one stacked limit) — a second limit must stack to match.
        bool prev_is_bigop = false;
        auto append = [&](LayoutBox value)
        {
            pieces.push_back(std::move(value));
            prev_was_operator = false;
            prev_is_bigop = false;
        };
        // Emits a symbol that may be a relation/binary operator, applying the same
        // spacing rule as the ordinary-glyph path so "\times" and "\leq" space
        // like "×" and "≤" typed directly.
        auto append_symbol = [&](const std::string &glyph)
        {
            const bool spaced = !text_mode && is_spaced_operator(glyph) && !pieces.empty() && !prev_was_operator;
            append(math_text(spaced ? " " + glyph + " " : glyph));
            prev_was_operator = !text_mode && is_spaced_operator(glyph);
            prev_is_bigop = is_big_operator(glyph);
        };

        while (pos < input.size() && input[pos] != stop)
        {
            const char c = input[pos];
            if (c == '{')
            {
                auto group = parse_math_group(input, pos, mode);
                if (group)
                    append(std::move(*group));
                else
                    append(math_text("{"));
                continue;
            }
            if (c == '^' || c == '_')
            {
                const bool superscript = c == '^';
                ++pos;
                LayoutBox script = math_text("");
                std::string raw_script_glyph;
                bool script_is_atomic = false;
                if (pos < input.size() && input[pos] == '{')
                {
                    auto group = parse_math_group(input, pos, mode);
                    if (group)
                        script = std::move(*group);
                }
                else if (pos < input.size() && input[pos] == '\\')
                {
                    // Parse control sequence like \mu or \nu as script target
                    size_t lookahead = pos + 1;
                    while (lookahead < input.size() && std::isalpha(static_cast<unsigned char>(input[lookahead])))
                        ++lookahead;
                    std::string macro_name(input.substr(pos + 1, lookahead - (pos + 1)));
                    pos = lookahead;
                    // Resolve to the symbol itself, so x^\mu reads "x^μ" and not
                    // the two letters "m" and "u" set in superscript.
                    auto it = math_symbols().find(macro_name);
                    raw_script_glyph = it != math_symbols().end() ? it->second : macro_name;
                    script_is_atomic = true;
                    script = math_text(raw_script_glyph);
                }
                else if (pos < input.size())
                {
                    raw_script_glyph = next_glyph(input, pos);
                    script = math_text(raw_script_glyph);
                }
                if (!pieces.empty())
                {
                    std::string text_to_map = !raw_script_glyph.empty()
                                                  ? raw_script_glyph
                                                  : (script.rows.size() == 1 ? script.rows.front() : "");
                    std::string mapped = script_glyphs(text_to_map, superscript, script_is_atomic);

                    // A clean mapping sits on the baseline already. So does the
                    // "^x"/"_x" prefix form, which is all inline math can afford —
                    // raising or lowering the run would cost it a second row.
                    const bool mapped_cleanly = !mapped.empty() &&
                                                mapped.find('^') == std::string::npos &&
                                                mapped.find('_') == std::string::npos;

                    // Big operators take stacked limits in display style, and both
                    // limits have to agree. Without this, \sum_{n=0}^{\infty} split
                    // its limits by accident: "n=0" maps cleanly to Unicode
                    // subscripts so it went inline, while "∞" has no superscript
                    // form so it was raised to its own row — giving "∑ₙ₌₀" with a
                    // stray "∞" floating above it.
                    const bool stack_on_bigop =
                        prev_is_bigop && mode == MathMode::Display;

                    if (stack_on_bigop)
                    {
                        pieces.back() =
                            stack_limit(pieces.back(), script, superscript);
                        prev_is_bigop = true; // the other limit must stack too
                        continue;
                    }

                    // Inline math has no second row to raise an exponent into, so
                    // an unshiftable superscript degrades to a parenthetical
                    // "^(...)" run. When the base is Euler's number e, write that
                    // as "exp(...)" instead: "e^(-0.5*((x-μ)/σ)²)" reads as a
                    // caret bound to the next token, "exp(...)" is unambiguously
                    // a function call.
                    const bool base_is_e = superscript &&
                                           mode == MathMode::Inline &&
                                           trim_spaces(flatten_box(pieces.back())) == "e";

                    if (base_is_e && !mapped_cleanly)
                    {
                        pieces.back() = math_text("exp(" + text_to_map + ")");
                    }
                    else if (script.rows.size() > 1)
                    {
                        // A multi-row script — a fraction, a nested exponent, a
                        // matrix — has no Unicode shift. Its own rows already
                        // carry the correct elevation, so stack it whole above
                        // or below the base instead of flattening it to a glyph
                        // run: flattening an empty text_to_map would discard the
                        // exponent entirely (e^{e^{x^2+1}} lost its inner e^{...}).
                        pieces.back() = stack_limit(pieces.back(), script, superscript);
                    }
                    else if (mapped_cleanly || mode == MathMode::Inline)
                    {
                        pieces.back() = math_hbox(pieces.back(), math_text(mapped));
                    }
                    else
                    {
                        // Fallback: strip leading ^ or _ prefix if script_glyphs added it
                        std::string clean_text = text_to_map;
                        if (!mapped.empty() && (mapped[0] == '^' || mapped[0] == '_'))
                        {
                            clean_text = mapped.substr(1);
                        }
                        LayoutBox elevated = math_text(clean_text);
                        if (superscript)
                        {
                            elevated.height_above = 1;
                            elevated.rows.push_back(spaces(elevated.width()));
                        }
                        else
                        {
                            elevated.height_below = 1;
                            elevated.rows.insert(
                                elevated.rows.begin(),
                                spaces(elevated.width()));
                        }
                        pieces.back() = math_hbox(pieces.back(), elevated);
                    }
                }
                continue;
            }
            if (c == '\\')
            {
                ++pos;
                const size_t name_start = pos;
                while (pos < input.size() && std::isalpha(static_cast<unsigned char>(input[pos])))
                    ++pos;
                const std::string name(input.substr(name_start, pos - name_start));
                if (name.empty() && pos < input.size())
                {
                    const char spacing = input[pos++];
                    if (spacing == ',' || spacing == ';')
                        append(math_text(" "));
                    else if (spacing == '\\')
                    {
                        if (allow_linebreaks && !text_mode)
                        {
                            // \\ ends the current display line. Consume the
                            // optional vertical-space suffix ("\\[2mm]").
                            if (pos < input.size() && input[pos] == '[')
                            {
                                const size_t close = input.find(']', pos);
                                if (close != std::string_view::npos)
                                    pos = close + 1;
                            }
                            completed_lines.push_back(
                                pieces.empty() ? math_text("") : math_hrow(pieces));
                            pieces.clear();
                            prev_was_operator = false;
                            prev_is_bigop = false;
                        }
                        else
                            append(math_text("  ")); // Matrix row separator
                    }
                    else if (spacing != '!')
                        append(math_text(std::string("\\") + spacing));
                }
                else if (name == "frac")
                {
                    auto numerator = parse_math_group(input, pos, mode);
                    auto denominator = parse_math_group(input, pos, mode);
                    if (numerator && denominator)
                        append(math_fraction(*numerator, *denominator, mode));
                    else
                        append(math_text("\\frac"));
                }
                else if (name == "sqrt")
                {
                    std::optional<LayoutBox> root_index;
                    if (pos < input.size() && input[pos] == '[')
                    {
                        ++pos;
                        root_index = parse_math_sequence(input, pos, ']', mode);
                        if (pos < input.size() && input[pos] == ']')
                            ++pos;
                    }
                    auto body = parse_math_group(input, pos, mode);
                    if (body && mode == MathMode::Inline)
                    {
                        // The overline needs a row above the body; inline math has
                        // none, so bracket the radicand instead.
                        const std::string radicand = trim_spaces(flatten_box(*body));
                        const std::string index = root_index
                                                      ? trim_spaces(flatten_box(*root_index))
                                                      : std::string();
                        const std::string prefix = index.empty() ? "√" : index + "√";
                        append(math_text(prefix + (needs_parens(radicand)
                                                       ? "(" + radicand + ")"
                                                       : radicand)));
                    }
                    else if (body)
                    {
                        // Radical sign on the baseline, overline as a text rule in
                        // the row above the radicand: "√" carries the hook, the
                        // rule covers what is under the root.
                        LayoutBox body_padded = std::move(*body);
                        const int radicand_width = body_padded.width();
                        body_padded.rows.insert(body_padded.rows.begin(),
                                                rule(radicand_width));
                        body_padded.height_above += 1;

                        const std::string index = root_index
                                                      ? trim_spaces(flatten_box(*root_index))
                                                      : std::string();
                        LayoutBox prefix = math_text(index + "√");
                        prefix.height_above = body_padded.height_above;
                        prefix.height_below = body_padded.height_below;
                        prefix.rows.assign(
                            static_cast<size_t>(body_padded.total_height()), "");
                        prefix.rows[static_cast<size_t>(prefix.height_above)] = "√";

                        append(math_hbox(prefix, body_padded));
                    }
                    else
                        append(math_text("\\sqrt"));
                }
                else if (name == "text" || name == "mathrm" || name == "mathbf" ||
                         name == "mathit" || name == "mathbr" ||
                         name == "mathsf" || name == "mathtt" || name == "mathnormal" ||
                         name == "bm" || name == "symbf" || name == "operatorname" ||
                         name == "boldsymbol" ||
                         // Annotation braces have no terminal equivalent, but
                         // their content is ordinary maths and their trailing
                         // _{label} still reads correctly as a subscript.
                         name == "underbrace" || name == "overbrace" ||
                         name == "underline" || name == "overline")
                {
                    // These commands affect typography/styling in full TeX. In a terminal,
                    // unwrapping and preserving the inner grouped text is cleaner than
                    // exposing raw macro tags.
                    //
                    // Parsed with text_mode = true: these bodies are prose ("\text{for
                    // all }x"), so their spaces are meaningful and must survive the
                    // whitespace stripping that applies to maths.
                    //
                    // \operatorname* (limits variant) is the same command here.
                    if (name == "operatorname" && pos < input.size() && input[pos] == '*')
                        ++pos;
                    auto body = parse_math_group(input, pos, mode, /*text_mode=*/true);
                    if (body)
                        append(std::move(*body));
                    else
                        append(math_text("\\" + name));
                }
                else if (name == "dot" || name == "ddot" || name == "hat" ||
                         name == "bar" || name == "vec" ||
                         name == "tilde" || name == "widetilde" ||
                         name == "widehat" || name == "check" ||
                         name == "breve" || name == "acute" ||
                         name == "grave" || name == "mathring" ||
                         name == "overrightarrow" || name == "overleftarrow")
                {
                    auto body = parse_math_group(input, pos, mode);
                    LayoutBox target;
                    if (body)
                        target = std::move(*body);
                    else if (pos < input.size())
                        target = math_text(next_glyph(input, pos));
                    if (!target.rows.empty())
                    {
                        // \hat now uses U+0302 COMBINING CIRCUMFLEX like its
                        // siblings, so \hat{H} sets as "\u0124" rather than the ASCII
                        // fallback "^H". Combining marks are zero-width and attach
                        // to the preceding glyph, which is why the accent is
                        // appended rather than prepended \u2014 the old prepended "^"
                        // was a separate spacing character and read as a caret.
                        const char *accent =
                            name == "dot"                              ? "\u0307"
                            : name == "ddot"                           ? "\u0308"
                            : name == "hat" || name == "widehat"       ? "\u0302"
                            : name == "bar"                            ? "\u0304"
                            : name == "tilde" || name == "widetilde"   ? "\u0303"
                            : name == "check"                          ? "\u030c"
                            : name == "breve"                          ? "\u0306"
                            : name == "acute"                          ? "\u0301"
                            : name == "grave"                          ? "\u0300"
                            : name == "mathring"                       ? "\u030a"
                            : name == "overleftarrow"                  ? "\u20d6"
                                                                       : "\u20d7"; // vec, overrightarrow
                        target.rows.front() += accent;
                        append(std::move(target));
                    }
                    else
                    {
                        append(math_text("\\" + name));
                    }
                }
                else if (name == "int" || name == "oint" || name == "iint" || name == "iiint")
                {
                    append(math_integral(1, name));
                }
                else if (name == "binom" || name == "dbinom" || name == "tbinom")
                {
                    auto top = parse_math_group(input, pos, mode);
                    auto bottom = parse_math_group(input, pos, mode);
                    if (top && bottom)
                    {
                        if (mode == MathMode::Inline)
                        {
                            // C(n, k) is the only unambiguous one-row form.
                            append(math_text("C(" + trim_spaces(flatten_box(*top)) +
                                             ", " + trim_spaces(flatten_box(*bottom)) + ")"));
                        }
                        else
                        {
                            LayoutBox stacked =
                                vstack_boxes({*top, *bottom}, /*centre=*/true);
                            LayoutBox left_col = make_delimiter_column(
                                "(", stacked.height_above, stacked.height_below);
                            LayoutBox right_col = make_delimiter_column(
                                ")", stacked.height_above, stacked.height_below);
                            append(math_hrow({std::move(left_col), std::move(stacked),
                                              std::move(right_col)}));
                        }
                    }
                    else
                        append(math_text("\\binom"));
                }
                else if (name == "overset" || name == "underset" || name == "stackrel")
                {
                    // \overset{anno}{base} / \stackrel{anno}{base}: annotation
                    // first, base second. \underset puts it below.
                    const bool above = name != "underset";
                    auto anno = parse_math_group(input, pos, mode);
                    auto base = parse_math_group(input, pos, mode);
                    if (anno && base)
                    {
                        if (mode == MathMode::Display)
                            append(stack_limit(*base, *anno, above));
                        else
                        {
                            const std::string a = trim_spaces(flatten_box(*anno));
                            const std::string b = trim_spaces(flatten_box(*base));
                            const std::string mapped = script_glyphs(a, above);
                            const bool clean = !mapped.empty() &&
                                               mapped.find('^') == std::string::npos &&
                                               mapped.find('_') == std::string::npos;
                            append(math_text(clean ? b + mapped
                                                   : b + "(" + a + ")"));
                        }
                    }
                    else
                        append(math_text("\\" + name));
                }
                else if (name == "substack")
                {
                    auto raw = read_brace_group_raw(input, pos);
                    if (raw)
                    {
                        std::vector<LayoutBox> rows;
                        std::string joined;
                        size_t rstart = 0;
                        while (rstart <= raw->size())
                        {
                            size_t rend = raw->find("\\\\", rstart);
                            if (rend == std::string_view::npos)
                                rend = raw->size();
                            size_t rpos = 0;
                            const std::string_view row_src =
                                raw->substr(rstart, rend - rstart);
                            LayoutBox row =
                                parse_math_sequence(row_src, rpos, '\0', MathMode::Inline);
                            const std::string flat = trim_spaces(flatten_box(row));
                            if (!flat.empty())
                            {
                                if (!joined.empty())
                                    joined += ", ";
                                joined += flat;
                                rows.push_back(math_text(flat));
                            }
                            if (rend == raw->size())
                                break;
                            rstart = rend + 2;
                        }
                        if (mode == MathMode::Display && rows.size() > 1)
                            append(vstack_boxes(rows, /*centre=*/true));
                        else
                            append(math_text(joined));
                    }
                    else
                        append(math_text("\\substack"));
                }
                else if (name == "xrightarrow" || name == "xleftarrow")
                {
                    std::optional<LayoutBox> below;
                    if (pos < input.size() && input[pos] == '[')
                    {
                        ++pos;
                        below = parse_math_sequence(input, pos, ']', mode);
                        if (pos < input.size() && input[pos] == ']')
                            ++pos;
                    }
                    auto above = parse_math_group(input, pos, mode);
                    const bool rightward = name == "xrightarrow";
                    const std::string label =
                        above ? trim_spaces(flatten_box(*above)) : std::string();
                    const std::string below_label =
                        below ? trim_spaces(flatten_box(*below)) : std::string();
                    if (mode == MathMode::Inline ||
                        (label.empty() && below_label.empty()))
                    {
                        // One row: the label rides inside the arrow shaft, ─Δ→.
                        std::string combined = label;
                        if (!below_label.empty())
                            combined += (combined.empty() ? "" : "/") + below_label;
                        std::string arrow;
                        if (combined.empty())
                            arrow = rightward ? "→" : "←";
                        else if (rightward)
                            arrow = "─" + combined + "→";
                        else
                            arrow = "←" + combined + "─";
                        append(math_text(arrow));
                    }
                    else
                    {
                        const int label_width = std::max(
                            ftxui::string_width(label),
                            ftxui::string_width(below_label));
                        LayoutBox shaft;
                        if (rightward)
                            shaft = math_text(rule(label_width + 1) + "→");
                        else
                            shaft = math_text("←" + rule(label_width + 1));
                        if (!label.empty())
                            shaft = stack_limit(shaft, math_text(label), /*above=*/true);
                        if (!below_label.empty())
                            shaft = stack_limit(shaft, math_text(below_label), /*above=*/false);
                        append(std::move(shaft));
                    }
                }
                else if (name == "phantom" || name == "hphantom" || name == "vphantom")
                {
                    auto body = parse_math_group(input, pos, mode);
                    if (body)
                    {
                        if (name == "vphantom")
                        {
                            // Zero width, the body's height.
                            LayoutBox ghost;
                            ghost.height_above = body->height_above;
                            ghost.height_below = body->height_below;
                            ghost.rows.assign(
                                static_cast<size_t>(body->total_height()), "");
                            append(std::move(ghost));
                        }
                        else
                            append(math_text(spaces(body->width())));
                    }
                }
                else if (name == "left")
                {
                    // Parse the enclosed body by recursion; the sub-parse stops
                    // at the matching \right (nested \left pairs spawn their
                    // own sub-parses, so pairing is automatic) and reports the
                    // closing delimiter through `close_delim`.
                    const std::string open_delim = read_delim_token(input, pos);
                    std::string close_delim;
                    LayoutBox body = parse_math_sequence(input, pos, stop, mode,
                                                         text_mode, &close_delim);
                    if (close_delim.empty())
                        close_delim = ".";
                    if (mode == MathMode::Inline || body.total_height() <= 1)
                    {
                        std::vector<LayoutBox> parts;
                        if (open_delim != ".")
                            parts.push_back(math_text(open_delim));
                        parts.push_back(std::move(body));
                        if (close_delim != ".")
                            parts.push_back(math_text(close_delim));
                        append(math_hrow(parts));
                    }
                    else
                    {
                        // Delimiters grow to the body's height (⎛⎜⎝ …).
                        LayoutBox left_col = make_delimiter_column(
                            open_delim, body.height_above, body.height_below);
                        LayoutBox right_col = make_delimiter_column(
                            close_delim, body.height_above, body.height_below);
                        append(math_hrow({std::move(left_col), std::move(body),
                                          std::move(right_col)}));
                    }
                }
                else if (name == "right")
                {
                    const std::string delim = read_delim_token(input, pos);
                    if (right_delim_out)
                    {
                        *right_delim_out = delim;
                        break; // ends the \left sub-parse
                    }
                    // Orphan \right: keep the delimiter as an ordinary glyph.
                    if (delim != ".")
                        append(math_text(delim));
                }
                else if (name == "middle")
                {
                    const std::string delim = read_delim_token(input, pos);
                    if (delim != ".")
                        append(math_text(delim));
                }
                else if (name == "begin")
                {
                    auto env = parse_math_group(input, pos, mode);
                    if (env)
                    {
                        const std::string env_name = trim_spaces(flatten_box(*env));
                        std::string base_name = env_name;
                        if (!base_name.empty() && base_name.back() == '*')
                            base_name.pop_back();
                        if (auto traits = env_traits(base_name))
                        {
                            std::string colspec;
                            if (base_name == "array")
                                if (auto raw = read_brace_group_raw(input, pos))
                                    colspec = std::string(*raw);
                            auto matrix_box = parse_math_matrix(
                                input, pos, env_name, *traits, colspec, mode);
                            if (matrix_box)
                            {
                                append(std::move(*matrix_box));
                                continue;
                            }
                        }
                        else
                        {
                            // Unknown environment: drop the begin/end tags and
                            // render the body — content beats leaked TeX.
                            size_t after_end = 0;
                            const size_t end_pos =
                                find_env_end(input, pos, env_name, after_end);
                            if (end_pos != std::string_view::npos)
                            {
                                std::string_view env_body =
                                    input.substr(pos, end_pos - pos);
                                size_t bpos = 0;
                                append(parse_math_sequence(
                                    env_body, bpos, '\0', mode, text_mode, nullptr,
                                    /*allow_linebreaks=*/mode == MathMode::Display));
                                pos = after_end;
                                continue;
                            }
                        }
                    }
                }
                else if (name == "end")
                {
                    auto env = parse_math_group(input, pos, mode);
                }
                else if (name == "pmatrix" || name == "vmatrix" || name == "bmatrix" || name == "matrix" || name == "aligned")
                {
                    // Silently consume standalone matrix tags
                }
                else if (name == "large" || name == "Large" || name == "LARGE" ||
                         name == "huge" || name == "Huge" || name == "small" ||
                         name == "tiny" || name == "normalsize" ||
                         name == "limits" || name == "nolimits" ||
                         name == "displaystyle" || name == "textstyle" ||
                         name == "scriptstyle" || name == "scriptscriptstyle" ||
                         name == "notag" || name == "nonumber")
                {
                    // Sizing/style/numbering directives: no terminal equivalent.
                }
                else if (name == "color" || name == "textcolor" ||
                         name == "label" || name == "tag")
                {
                    // Consume and drop the argument ({red}, {eq:foo}, {1a}).
                    if (name == "tag" && pos < input.size() && input[pos] == '*')
                        ++pos;
                    auto dropped = parse_math_group(input, pos, mode, /*text_mode=*/true);
                    (void)dropped;
                }
                else if (name == "mathbb" || name == "mathfrak" ||
                         name == "mathscr" || name == "mathcal")
                {
                    auto body = parse_math_group(input, pos, mode);
                    if (body)
                    {
                        const MathAlphabet which =
                            name == "mathbb"     ? MathAlphabet::DoubleStruck
                            : name == "mathfrak" ? MathAlphabet::Fraktur
                                                 : MathAlphabet::Script;
                        for (auto &row : body->rows)
                            row = map_math_alphabet(row, which);
                        append(std::move(*body));
                    }
                    else
                        append(math_text("\\" + name));
                }
                else if (name == "quad" || name == "qquad")
                {
                    append(math_text(name == "quad" ? "  " : "    "));
                }
                else if (name == "bmod")
                {
                    append(math_text(" mod "));
                }
                else if (name == "pmod")
                {
                    auto body = parse_math_group(input, pos, mode);
                    append(math_text(body ? " (mod " + flatten_box(*body) + ")"
                                          : " (mod)"));
                }
                else
                {
                    if (auto it = math_symbols().find(name); it != math_symbols().end())
                        append_symbol(it->second);
                    else if (name.size() > 1)
                        // Unknown multi-letter command: the bare name reads
                        // better than leaked TeX syntax ("\foo x" → "foo x"),
                        // matching how det/sin/lim already render. Single
                        // letters keep the backslash — "\a" as "a" would
                        // silently change meaning.
                        append(math_text(name));
                    else
                        append(math_text("\\" + name));
                }
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                // TeX ignores source whitespace in math mode entirely — spacing is
                // a property of the atoms, not of how the author typed them. This
                // used to emit a literal space per whitespace character, so
                // "G_{\mu \nu}" rendered as "G_μ ν" and "\frac{8 \pi G}{c^4}" as
                // "8 π G", both of which real LaTeX sets tight. Spacing that
                // *should* appear is added around relations and binary operators
                // below, which is where it comes from in TeX too.
                //
                // \text{...} bodies are prose, not maths, and keep their spaces.
                while (pos < input.size() &&
                       std::isspace(static_cast<unsigned char>(input[pos])))
                    ++pos;
                if (text_mode)
                    append(math_text(" "));
                continue;
            }
            {
                std::string glyph = next_glyph(input, pos);
                const bool spaced = !text_mode && is_spaced_operator(glyph) && !pieces.empty() && !prev_was_operator;
                if (spaced)
                {
                    append(math_text(" " + glyph + " "));
                }
                else if (!text_mode && (glyph == "," || glyph == ";"))
                {
                    // Punctuation carries a trailing space in TeX, which is what
                    // makes "x_1, x_2, \ldots, x_n" read as a list. Source
                    // whitespace no longer supplies it (see the whitespace branch
                    // above), so the atom has to bring its own.
                    append(math_text(glyph + " "));
                }
                else
                {
                    append(math_text(glyph));
                }
                prev_was_operator = !text_mode && is_spaced_operator(glyph);
                continue;
            }
        }

        if (!completed_lines.empty())
        {
            // \\ broke the sequence into display lines; stack them left-aligned.
            if (!pieces.empty())
                completed_lines.push_back(math_hrow(pieces));
            return vstack_boxes(completed_lines, /*centre=*/false);
        }
        if (pieces.empty())
            return math_text("");
        return math_hrow(pieces);
    }

    std::optional<LayoutBox> parse_math_group(std::string_view input, size_t &pos, MathMode mode,
                                              bool text_mode)
    {
        if (pos >= input.size() || input[pos] != '{')
            return std::nullopt;
        ++pos;
        auto result = parse_math_sequence(input, pos, '}', mode, text_mode);
        if (pos >= input.size() || input[pos] != '}')
            return std::nullopt;
        ++pos;
        return result;
    }

    // Inline math reduced to its single row of text. Callers that need to measure
    // or wrap math alongside ordinary prose work with this rather than an Element.
    std::string math_to_text(std::string_view source)
    {
        size_t pos = 0;
        LayoutBox layout = parse_math_sequence(source, pos, '\0', MathMode::Inline);
        return layout.rows.empty() ? std::string() : flatten_box(layout);
    }

    std::optional<std::pair<size_t, size_t>> find_math_span(std::string_view text, size_t from);

    // Replace every math span in `text` with its rendered inline form, leaving the
    // surrounding prose untouched.
    std::string substitute_inline_math(std::string_view text)
    {
        std::string out;
        size_t cursor = 0;
        while (cursor < text.size())
        {
            auto span = find_math_span(text, cursor);
            if (!span)
            {
                out.append(text.substr(cursor));
                break;
            }
            const auto [open, end] = *span;
            out.append(text.substr(cursor, open - cursor));
            const size_t delim = text[open + 1] == '$' ? 2 : 1;
            out += math_to_text(text.substr(open + delim, (end - delim) - (open + delim)));
            cursor = end;
        }
        return out;
    }

    ftxui::Element render_math(std::string_view source, ftxui::Decorator style, MathMode mode)
    {
        size_t pos = 0;
        LayoutBox layout = parse_math_sequence(source, pos, '\0', mode,
                                               /*text_mode=*/false,
                                               /*right_delim_out=*/nullptr,
                                               /*allow_linebreaks=*/mode == MathMode::Display);
        if (layout.rows.empty())
            return ftxui::text("") | style;
        // Inline math must occupy exactly one row: a taller element would set the
        // height of the whole flexbox line and push the surrounding prose beneath it.
        if (mode == MathMode::Inline)
            return ftxui::text(flatten_box(layout)) | style;
        // One text element per row. This used to be an ftxui::canvas, which draws
        // on a sub-cell grid of 2x4 pixels per character and renders those pixels
        // as braille — fine for plots, but it turned the radical's overline into a
        // run of dot glyphs sitting above the equation. Rows are already laid out
        // on the character grid, so plain text is both correct and simpler.
        ftxui::Elements lines;
        lines.reserve(layout.rows.size());
        for (auto &row : layout.rows)
            lines.push_back(ftxui::text(std::move(row)));
        if (lines.empty())
            lines.push_back(ftxui::text(""));
        return ftxui::vbox(std::move(lines)) | style;
    }

    bool escaped_at(std::string_view text, size_t pos)
    {
        size_t slashes = 0;
        while (pos > 0 && text[pos - 1] == '\\')
        {
            ++slashes;
            --pos;
        }
        return (slashes % 2) != 0;
    }

    std::optional<std::pair<size_t, size_t>> find_math_span(std::string_view text,
                                                            size_t from)
    {
        size_t open = text.find('$', from);
        while (open != std::string_view::npos && escaped_at(text, open))
            open = text.find('$', open + 1);
        if (open == std::string_view::npos)
            return std::nullopt;
        const size_t delimiter_size = (open + 1 < text.size() && text[open + 1] == '$') ? 2 : 1;

        if (delimiter_size == 2)
        {
            size_t close = text.find("$$", open + 2);
            while (close != std::string_view::npos && escaped_at(text, close))
                close = text.find("$$", close + 1);
            if (close == std::string_view::npos)
                return std::nullopt;
            return std::make_pair(open, close + 2);
        }
        else
        {
            // Single $ inline math cannot cross newlines
            size_t next_newline = text.find('\n', open + 1);
            size_t close = text.find('$', open + 1);
            while (close != std::string_view::npos && (next_newline == std::string_view::npos || close < next_newline))
            {
                if (!escaped_at(text, close) && (close + 1 >= text.size() || text[close + 1] != '$'))
                    return std::make_pair(open, close + 1);
                close = text.find('$', close + 1);
            }
            return std::nullopt;
        }
    }

    bool contains_math(std::string_view text)
    {
        return find_math_span(text, 0).has_value();
    }

} // namespace ftxui::ext
