#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ftxui::ext
{

    enum class HighlightTag
    {
        None,
        Keyword,
        Type,
        Function,
        String,
        Number,
        Comment,
        Operator,
        Constant,
        Variable,
        Punctuation
    };

    struct HighlightSpan
    {
        size_t start_byte{0};
        size_t end_byte{0};
        HighlightTag tag{HighlightTag::None};
    };

    struct SyntaxStyle
    {
        ftxui::Color keyword{ftxui::Color::Magenta};
        ftxui::Color type{ftxui::Color::Cyan};
        ftxui::Color function{ftxui::Color::BlueLight};
        ftxui::Color string{ftxui::Color::Green};
        ftxui::Color number{ftxui::Color::Yellow};
        ftxui::Color comment{ftxui::Color::GrayDark};
        ftxui::Color op{ftxui::Color::White};
        ftxui::Color constant{ftxui::Color::RedLight};
        ftxui::Color variable{ftxui::Color::Default};
        ftxui::Color punctuation{ftxui::Color::GrayLight};
        ftxui::Color default_fg{ftxui::Color::Yellow};
    };

    /// Check if the given language tag has Tree-sitter highlighting support.
    bool supports_syntax_highlighting(std::string_view language);

    /// Compute syntax highlight spans for source code in the given language.
    /// Returns non-overlapping, in-order spans covering the entire text [0, source.size()).
    std::vector<HighlightSpan> highlight_syntax(std::string_view source, std::string_view language);

    /// Convert a HighlightTag to its FTXUI Decorator style.
    ftxui::Decorator style_for_tag(HighlightTag tag, const SyntaxStyle &style);

} // namespace ftxui::ext
