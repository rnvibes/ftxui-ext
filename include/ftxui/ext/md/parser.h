#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ftxui::ext::md
{

    enum class InlineKind
    {
        Text,
        Bold,
        Italic,
        Code,
        Math, // inline $...$, already rendered to terminal glyphs
    };

    struct Inline
    {
        InlineKind kind = InlineKind::Text;
        std::string text;
        // Math rendered to terminal glyphs cannot also be bolded -- the
        // subscript and superscript characters have no bold forms -- so
        // emphasis around a math span is carried as colour instead, and the
        // ** markers are dropped rather than shown literally.
        bool emphasized = false;
    };

    enum class BlockKind
    {
        Paragraph,
        Heading,
        CodeBlock,
        Math,       // display $$...$$ occupying its own block
        Table,      // GFM pipe table — not CommonMark, but universal in practice
        List,       // one contiguous run of bullets or numbers
        Blockquote, // one contiguous run of "> " lines
        Rule,       // --- / *** / ___
    };

    // One table row's cells, each already split into inline spans.
    using TableRow = std::vector<std::vector<Inline>>;

    struct Block
    {
        BlockKind kind = BlockKind::Paragraph;
        int level = 0;             // Heading: 1..6
        std::string language;      // CodeBlock: the fence's info string
        std::string literal;       // CodeBlock body, or Math's LaTeX source
        std::vector<Inline> spans; // Paragraph / Heading / Blockquote

        // Table
        TableRow headers;
        std::vector<TableRow> rows;

        // List
        bool ordered = false;
        int start = 1;
        std::vector<std::vector<Inline>> items;
    };

    using Document = std::vector<Block>;

    // Line-oriented markdown parser covering what a chat reply actually
    // contains: ATX headings, fenced code, paragraphs, and the inline set
    // bold/italic/code/math. Plain values throughout — a Document is freely
    // copyable and movable.
    //
    // Tolerant of truncation by design: this is re-run on every streamed
    // token, so an unterminated fence or emphasis run renders as the partial
    // construct rather than falling back to raw text.
    Document parse(std::string_view source);

} // namespace ftxui::ext::md
