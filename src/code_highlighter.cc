#include "ftxui/ext/code_highlighter.h"

#ifdef SURI_CHAT_HAS_TREESITTER
#include "suri/code/language_registry.h"
#endif

namespace ftxui::ext
{

    namespace
    {

#ifdef SURI_CHAT_HAS_TREESITTER
        HighlightTag ConvertTag(suri::code::HighlightTag tag)
        {
            switch (tag)
            {
            case suri::code::HighlightTag::Keyword:     return HighlightTag::Keyword;
            case suri::code::HighlightTag::Type:        return HighlightTag::Type;
            case suri::code::HighlightTag::Function:    return HighlightTag::Function;
            case suri::code::HighlightTag::String:      return HighlightTag::String;
            case suri::code::HighlightTag::Number:      return HighlightTag::Number;
            case suri::code::HighlightTag::Comment:     return HighlightTag::Comment;
            case suri::code::HighlightTag::Operator:    return HighlightTag::Operator;
            case suri::code::HighlightTag::Constant:    return HighlightTag::Constant;
            case suri::code::HighlightTag::Variable:    return HighlightTag::Variable;
            case suri::code::HighlightTag::Punctuation: return HighlightTag::Punctuation;
            case suri::code::HighlightTag::None:        return HighlightTag::None;
            }
            return HighlightTag::None;
        }
#endif

    } // namespace

    bool supports_syntax_highlighting(std::string_view language)
    {
#ifdef SURI_CHAT_HAS_TREESITTER
        return suri::code::LanguageRegistry::instance().supports_highlighting(language);
#else
        // Tree-sitter is compiled out: nothing is highlighted, so no language
        // is supported. Callers treat this as "render plain" (md/render.cc).
        (void)language;
        return false;
#endif
    }

    std::vector<HighlightSpan> highlight_syntax(std::string_view source, std::string_view language)
    {
        if (source.empty())
            return {};

#ifdef SURI_CHAT_HAS_TREESITTER
        auto core_spans = suri::code::LanguageRegistry::instance().highlight(source, language);
        std::vector<HighlightSpan> out;
        out.reserve(core_spans.size());

        for (const auto &cs : core_spans)
        {
            out.push_back(HighlightSpan{
                .start_byte = cs.start_byte,
                .end_byte = cs.end_byte,
                .tag = ConvertTag(cs.tag)
            });
        }

        return out;
#else
        (void)language;
        return {};
#endif
    }

    ftxui::Decorator style_for_tag(HighlightTag tag, const SyntaxStyle &style)
    {
        switch (tag)
        {
        case HighlightTag::Keyword:     return ftxui::color(style.keyword) | ftxui::bold;
        case HighlightTag::Type:        return ftxui::color(style.type);
        case HighlightTag::Function:    return ftxui::color(style.function);
        case HighlightTag::String:      return ftxui::color(style.string);
        case HighlightTag::Number:      return ftxui::color(style.number);
        case HighlightTag::Comment:     return ftxui::color(style.comment) | ftxui::italic;
        case HighlightTag::Operator:    return ftxui::color(style.op);
        case HighlightTag::Constant:    return ftxui::color(style.constant);
        case HighlightTag::Variable:    return ftxui::color(style.variable);
        case HighlightTag::Punctuation: return ftxui::color(style.punctuation);
        case HighlightTag::None:        return ftxui::color(style.default_fg);
        }
        return ftxui::color(style.default_fg);
    }

} // namespace ftxui::ext
