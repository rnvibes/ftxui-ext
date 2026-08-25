#pragma once

#include <ftxui/dom/elements.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

// LaTeX-to-terminal-Unicode math renderer. Standalone from the markdown
// parser: it operates purely on $...$/$$...$$-delimited source text and
// produces character-grid ftxui::Elements, with no dependency on MdDocument
// or any particular markdown pipeline. Canvas rendering was tried first and
// abandoned — braille sub-cell pixels turned radical overlines into runs of
// dots that didn't read as a line next to ordinary terminal text.
namespace ftxui::ext
{

    // How a math span is laid out. Display math gets a 2D stacked layout (real
    // fraction bars, raised/lowered scripts, multi-row matrices) and is only
    // safe where the span owns its whole block. Inline math is constrained to
    // a single row so it can sit in the middle of a paragraph or list item
    // without forcing the surrounding text onto the lines below it.
    enum class MathMode
    {
        Display,
        Inline
    };

    // Rewrite LaTeX bracket delimiters to the dollar forms the renderer scans
    // for: \( x \) becomes $ x $ and \[ x \] becomes $$ x $$. Also escapes
    // punctuation inside every math span so the markdown parser returns it
    // byte-for-byte instead of reading "_" as emphasis or "\," as an escaped
    // comma, and wraps a bare top-level \begin{env}...\end{env} (for
    // environments this renderer understands) in $$, since LLM output
    // routinely omits the surrounding math delimiters entirely.
    //
    // This has to run on the markdown source, before parsing — by the time a
    // document is built, "\(" has already been read as an escaped literal
    // paren and the backslash is gone, making the delimiter indistinguishable
    // from ordinary parenthesized text. Fenced/inline code is left untouched,
    // and a bracket pair is only rewritten when it actually encloses
    // something math-shaped, so prose using escaped brackets ("\[see
    // below\]") survives untouched.
    std::string NormalizeMathDelimiters(std::string_view source);

    // True if `text` contains an unescaped $...$ or $$...$$ span.
    bool contains_math(std::string_view text);

    // The next math span in `text` at or after `from`, as a [open, end) byte
    // range spanning the delimiters. std::nullopt if none (including an
    // unmatched opening `$`, which is left as literal text).
    std::optional<std::pair<size_t, size_t>> find_math_span(std::string_view text, size_t from);

    // Render LaTeX source (the text between the $ delimiters, not including
    // them) to a character-grid ftxui::Element. `style` is applied to the
    // whole result. Inline mode always returns something exactly one row
    // tall; Display mode may return multiple stacked rows.
    ftxui::Element render_math(std::string_view source, ftxui::Decorator style, MathMode mode);

    // Replace every math span in `text` with its rendered inline (flattened,
    // single-line) form, leaving surrounding prose untouched. For contexts
    // like table cells that build plain strings rather than hosting a
    // multi-row ftxui::Element.
    std::string substitute_inline_math(std::string_view text);

    // Advance `pos` past one UTF-8 codepoint in `input` and return its bytes.
    // General-purpose (not math-specific) — exposed because it's the same
    // glyph-walking primitive a caller needs to wrap prose to a terminal
    // column width without splitting a multibyte character.
    std::string next_glyph(std::string_view input, size_t &pos);

} // namespace ftxui::ext
