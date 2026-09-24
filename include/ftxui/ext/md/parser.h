#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ftxui::ext
{

    enum class MdInlineKind
    {
        Text,
        Bold,
        Italic,
        Code,
        Math, // inline $...$, already rendered to terminal glyphs
    };

    struct MdInline
    {
        MdInlineKind kind = MdInlineKind::Text;
        std::string text;
        // Math rendered to terminal glyphs cannot also be bolded -- the
        // subscript and superscript characters have no bold forms -- so
        // emphasis around a math span is carried as colour instead, and the
        // ** markers are dropped rather than shown literally.
        bool emphasized = false;
    };

    enum class MdBlockKind
    {
        Paragraph,
        Heading,
        CodeBlock,
        Math,       // display $$...$$ occupying its own block
        Table,      // GFM pipe table — not CommonMark, but universal in practice
        List,       // one contiguous run of bullets or numbers
        Blockquote, // one contiguous run of "> " lines
        Rule,       // --- / *** / ___
        Stage,      // a stage fence: a bordered status readout, never code
    };

    // One table row's cells, each already split into inline spans.
    using MdTableRow = std::vector<std::vector<MdInline>>;

    struct MdBlock
    {
        MdBlockKind kind = MdBlockKind::Paragraph;
        int level = 0;             // Heading: 1..6
        std::string language;      // CodeBlock: the fence's info string
        std::string literal;       // CodeBlock body, or Math's LaTeX source
        std::vector<MdInline> spans; // Paragraph / Heading / Blockquote

        // Table
        MdTableRow headers;
        std::vector<MdTableRow> rows;

        // List
        bool ordered = false;
        int start = 1;
        std::vector<std::vector<MdInline>> items;
    };

    using MdDocument = std::vector<MdBlock>;

    // Line-oriented markdown parser covering what a chat reply actually
    // contains: ATX headings, fenced code, paragraphs, and the inline set
    // bold/italic/code/math. Plain values throughout — a MdDocument is freely
    // copyable and movable.
    //
    // Tolerant of truncation by design: this is re-run on every streamed
    // token, so an unterminated fence or emphasis run renders as the partial
    // construct rather than falling back to raw text.
    MdDocument parse_markdown(std::string_view source);

} // namespace ftxui::ext
