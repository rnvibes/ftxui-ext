// ftxui-ext/include/ftxui/ext/org/doc.h — org-flavored document model.
//
// The org renderer consumes this document, not the org AST directly: a thin
// adapter (lives with the org parser) maps AST nodes onto these blocks, so
// ftxui-ext stays free of any particular org parser. The block shapes mirror
// the markdown document so both renderers lay content out identically; the
// inline kinds carry org's component colors.
#pragma once

#include <string>
#include <vector>

namespace ftxui::ext::org
{

    enum class InlineKind
    {
        Text,
        Bold,
        Italic,
        Underline,
        Strike,
        Code,
        Verbatim,
        Link,
        Todo,
        Done,
        Priority,
        Tag,
        TimestampActive,
        TimestampInactive,
        TimestampDiary,
        PlanningKey,
        DrawerName,
        PropertyKey,
        KeywordKey,
        Clock,
        Duration,
        LogState,
        Computed, // table-engine result
    };

    struct Inline
    {
        InlineKind kind = InlineKind::Text;
        std::string text;
    };

    enum class BlockKind
    {
        Headline,
        Paragraph,
        Table,
        List,
        Src,     // #+BEGIN_SRC ... #+END_SRC
        Quote,   // #+BEGIN_QUOTE ... #+END_QUOTE
        Example, // #+BEGIN_EXAMPLE ... #+END_EXAMPLE
        Keyword,
        DrawerOpen,  // ":NAME:" opener line
        DrawerEnd,   // ":END:" line
        Planning,    // DEADLINE:/SCHEDULED:/CLOSED: line
        Clock,       // CLOCK: line
    };

    struct TableCell
    {
        std::string text;
        bool computed = false; // table-engine result, styled with theme.computed
    };

    struct ListItem
    {
        std::string marker; // "• ", "3. ", "[X] ", "  • " (indent included)
        std::vector<Inline> spans;
    };

    struct Block
    {
        BlockKind kind = BlockKind::Paragraph;
        int level = 0;                       // Headline
        // True when a blank line separates this block from the previous one
        // in the source; the renderer only inserts spacing then, so org
        // documents keep their source rhythm (no gap between a headline and
        // its planning line, unlike markdown's always-spaced blocks).
        bool gap = false;
        std::vector<Inline> spans;           // Headline/Paragraph/Quote/Keyword/Planning/Clock
        std::vector<TableCell> headers;      // Table (first row)
        std::vector<std::vector<TableCell>> rows;
        std::vector<ListItem> items;         // List
        std::string language;                // Src
        std::string literal;                 // Src/Example body
        std::string name;                    // DrawerOpen
    };

    struct Document
    {
        std::vector<Block> blocks;
    };

} // namespace ftxui::ext::org
