// ftxui-ext/include/ftxui/ext/org/doc.h - the org-flavored document.
//
// OrgDocument derives from the shared ftxui::ext::Document spine: all
// block text is a string_view into either the source (zero-copy) or the
// document arena (synthesized markers, prefixes, computed values), and
// every block/vector is allocated from that arena. The renderer is
// parser-agnostic; a builder (lives with the org parser) fills the
// blocks. The block shapes mirror the markdown document so both
// renderers lay content out identically; the inline kinds carry org's
// component colors.
#pragma once

#include "ftxui/ext/document.h"

#include <memory_resource>
#include <string_view>
#include <vector>

namespace ftxui::ext
{
    enum class OrgInlineKind
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

    // One styled text run; text is a view into the source or the arena.
    struct OrgInline
    {
        OrgInlineKind kind = OrgInlineKind::Text;
        std::string_view text;
    };

    struct OrgTableCell
    {
        std::string_view text;
        bool computed = false; // table-engine result, styled with theme.computed
    };

    struct OrgListItem
    {
        std::string_view marker; // "• ", "3. ", "[X] ", "  • " (indent included)
        std::pmr::vector<OrgInline> spans;
        explicit OrgListItem(std::pmr::memory_resource* arena = std::pmr::get_default_resource())
            : spans(arena) {}
    };

    enum class OrgBlockKind
    {
        Headline,
        Paragraph,
        Table,
        List,
        Src,     // #+BEGIN_SRC ... #+END_SRC
        Quote,   // #+BEGIN_QUOTE ... #+END_QUOTE
        Example, // #+BEGIN_EXAMPLE ... #+END_EXAMPLE
        Keyword,
        DrawerOpen, // ":NAME:" opener line
        DrawerEnd,  // ":END:" line
        Planning,   // DEADLINE:/SCHEDULED:/CLOSED: line
        Clock,      // CLOCK: line
    };

    struct OrgBlock
    {
        OrgBlockKind kind = OrgBlockKind::Paragraph;
        // True when a blank line separates this block from the previous one
        // in the source; the renderer only inserts spacing then, so org
        // documents keep their source rhythm (no gap between a headline and
        // its planning line, unlike markdown's always-spaced blocks).
        bool gap = false;
        int level = 0; // Headline
        std::string_view language; // Src
        std::string_view literal;  // Src/Example body
        std::string_view name;     // DrawerOpen
        std::pmr::vector<OrgInline> spans;   // Headline/Paragraph/Quote/Keyword/Planning/Clock
        std::pmr::vector<OrgTableCell> headers; // Table (first row)
        std::pmr::vector<std::pmr::vector<OrgTableCell>> rows;
        std::pmr::vector<OrgListItem> items; // List

        explicit OrgBlock(std::pmr::memory_resource* arena = std::pmr::get_default_resource())
            : spans(arena), headers(arena), rows(arena), items(arena) {}
    };

    struct OrgDocument : public ftxui::ext::Document
    {
        explicit OrgDocument(std::string_view source = {}) : Document(source) {}

        OrgDocument(OrgDocument&&) noexcept = default;

        // The blocks are a plain vector; each OrgBlock's inner vectors are
        // arena-allocated. A move assignment must destroy our elements
        // while our arena is still owned, adopt the other's arena, then
        // steal its blocks (whose inner allocators now match our arena).
        OrgDocument& operator=(OrgDocument&& other) noexcept
        {
            if (this != &other)
            {
                blocks.clear(); // element dtors deallocate from our arena
                static_cast<ftxui::ext::Document&>(*this) = std::move(other);
                blocks = std::move(other.blocks);
            }
            return *this;
        }

        std::vector<OrgBlock> blocks;
    };
} // namespace ftxui::ext
