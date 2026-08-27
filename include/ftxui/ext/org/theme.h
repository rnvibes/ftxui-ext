// ftxui-ext/include/ftxui/ext/org/theme.h — colorscheme for the org renderer.
//
// Shared constructs reuse the markdown theme's field names (heading1-3,
// table_header/table_border, list_marker, code fg/bg, border, blockquote,
// bold/italic fg) so one [colorscheme] section in suri.toml styles both;
// org-specific components (todo/done/priority/tag/timestamps/planning/
// drawers/properties/keywords/computed) are additional keys.
#pragma once

#include "ftxui/ext/code_highlighter.h"

#include <ftxui/screen/color.hpp>

namespace ftxui::ext
{

    struct OrgTheme
    {
        // shared with the markdown theme
        ftxui::Color heading1 = ftxui::Color::Magenta;
        ftxui::Color heading2 = ftxui::Color::Blue;
        ftxui::Color heading3 = ftxui::Color::Cyan;
        ftxui::Color bold_fg = ftxui::Color::Default;
        ftxui::Color italic_fg = ftxui::Color::Default;
        ftxui::Color code_fg = ftxui::Color::Yellow;
        ftxui::Color code_bg = ftxui::Color::Black;
        ftxui::Color border = ftxui::Color::GrayDark;
        ftxui::Color truncation = ftxui::Color::Red;
        ftxui::Color table_header = ftxui::Color::Cyan;
        ftxui::Color table_border = ftxui::Color::GrayDark;
        ftxui::Color blockquote = ftxui::Color::GrayLight;
        ftxui::Color list_marker = ftxui::Color::Blue;

        // org-specific components
        ftxui::Color todo = ftxui::Color::Red;
        ftxui::Color done = ftxui::Color::Green;
        ftxui::Color priority = ftxui::Color::Yellow;
        ftxui::Color tag = ftxui::Color::BlueLight;
        ftxui::Color link = ftxui::Color::BlueLight;
        ftxui::Color timestamp_active = ftxui::Color::Yellow;
        ftxui::Color timestamp_inactive = ftxui::Color::GrayLight;
        ftxui::Color timestamp_diary = ftxui::Color::Green;
        ftxui::Color planning_key = ftxui::Color::Cyan;
        ftxui::Color drawer = ftxui::Color::GrayLight;
        ftxui::Color property_key = ftxui::Color::Cyan;
        ftxui::Color keyword = ftxui::Color::Magenta;
        ftxui::Color clock = ftxui::Color::BlueLight;
        ftxui::Color duration = ftxui::Color::Yellow;
        ftxui::Color log_state = ftxui::Color::Yellow;
        ftxui::Color computed = ftxui::Color::Green;

        // syntax highlighting inside source blocks (same palette as markdown)
        ftxui::Color syn_keyword = ftxui::Color::Magenta;
        ftxui::Color syn_type = ftxui::Color::Cyan;
        ftxui::Color syn_function = ftxui::Color::BlueLight;
        ftxui::Color syn_string = ftxui::Color::Green;
        ftxui::Color syn_number = ftxui::Color::Yellow;
        ftxui::Color syn_comment = ftxui::Color::GrayDark;
        ftxui::Color syn_operator = ftxui::Color::White;
        ftxui::Color syn_constant = ftxui::Color::RedLight;
        ftxui::Color syn_variable = ftxui::Color::Default;
        ftxui::Color syn_punctuation = ftxui::Color::GrayLight;

        [[nodiscard]] ftxui::ext::SyntaxStyle syntax_style() const
        {
            ftxui::ext::SyntaxStyle s;
            s.keyword = syn_keyword;
            s.type = syn_type;
            s.function = syn_function;
            s.string = syn_string;
            s.number = syn_number;
            s.comment = syn_comment;
            s.op = syn_operator;
            s.constant = syn_constant;
            s.variable = syn_variable;
            s.punctuation = syn_punctuation;
            s.default_fg = code_fg;
            return s;
        }

        static OrgTheme Dark() { return OrgTheme{}; }

        static OrgTheme Light()
        {
            OrgTheme t;
            // shared palette mirrors the markdown light theme
            t.heading1 = ftxui::Color::RGB(0x82, 0x50, 0xDF);
            t.heading2 = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.heading3 = ftxui::Color::RGB(0x1A, 0x7F, 0x37);
            t.code_fg = ftxui::Color::RGB(0x1F, 0x23, 0x28);
            t.code_bg = ftxui::Color::RGB(0xF6, 0xF8, 0xFA);
            t.border = ftxui::Color::RGB(0xD0, 0xD7, 0xDE);
            t.table_header = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.table_border = ftxui::Color::RGB(0xD0, 0xD7, 0xDE);
            t.blockquote = ftxui::Color::RGB(0x57, 0x60, 0x6A);
            t.list_marker = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.todo = ftxui::Color::RGB(0xCF, 0x22, 0x2E);
            t.done = ftxui::Color::RGB(0x1A, 0x7F, 0x37);
            t.priority = ftxui::Color::RGB(0x9E, 0x6A, 0x03);
            t.tag = ftxui::Color::RGB(0x82, 0x50, 0xDF);
            t.link = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.timestamp_active = ftxui::Color::RGB(0x9E, 0x6A, 0x03);
            t.timestamp_inactive = ftxui::Color::RGB(0x6E, 0x77, 0x81);
            t.timestamp_diary = ftxui::Color::RGB(0x1A, 0x7F, 0x37);
            t.planning_key = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.drawer = ftxui::Color::RGB(0x6E, 0x77, 0x81);
            t.property_key = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.keyword = ftxui::Color::RGB(0x82, 0x50, 0xDF);
            t.clock = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.duration = ftxui::Color::RGB(0x9E, 0x6A, 0x03);
            t.log_state = ftxui::Color::RGB(0x9E, 0x6A, 0x03);
            t.computed = ftxui::Color::RGB(0x1A, 0x7F, 0x37);

            t.syn_keyword = ftxui::Color::RGB(0xCF, 0x22, 0x2E);
            t.syn_type = ftxui::Color::RGB(0x95, 0x38, 0x00);
            t.syn_function = ftxui::Color::RGB(0x82, 0x50, 0xDF);
            t.syn_string = ftxui::Color::RGB(0x0A, 0x30, 0x69);
            t.syn_number = ftxui::Color::RGB(0x05, 0x50, 0xAE);
            t.syn_comment = ftxui::Color::RGB(0x6E, 0x77, 0x81);
            t.syn_operator = ftxui::Color::RGB(0x24, 0x29, 0x2F);
            t.syn_constant = ftxui::Color::RGB(0x05, 0x50, 0xAE);
            t.syn_variable = ftxui::Color::RGB(0x1F, 0x23, 0x28);
            t.syn_punctuation = ftxui::Color::RGB(0x57, 0x60, 0x6A);
            return t;
        }
    };

} // namespace ftxui::ext
