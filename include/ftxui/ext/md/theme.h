#pragma once

#include "ftxui/ext/code_highlighter.h"

#include <ftxui/screen/color.hpp>

namespace ftxui::ext::md
{

    struct Theme
    {
        ftxui::Color heading1 = ftxui::Color::Magenta;
        ftxui::Color heading2 = ftxui::Color::Blue;
        ftxui::Color heading3 = ftxui::Color::Cyan;
        ftxui::Color bold_fg = ftxui::Color::Default;
        ftxui::Color italic_fg = ftxui::Color::Default;
        ftxui::Color code_fg = ftxui::Color::Yellow;
        ftxui::Color code_bg = ftxui::Color::Black;
        ftxui::Color math_fg = ftxui::Color::Green;
        ftxui::Color math_emphasis = ftxui::Color::GreenLight;
        ftxui::Color border = ftxui::Color::GrayDark;
        ftxui::Color truncation = ftxui::Color::Red;
        ftxui::Color table_header = ftxui::Color::Cyan;
        ftxui::Color table_border = ftxui::Color::GrayDark;
        ftxui::Color blockquote = ftxui::Color::GrayLight;
        ftxui::Color list_marker = ftxui::Color::Blue;
        ftxui::Color line_number = ftxui::Color::GrayDark;
        ftxui::Color cursor_line_number = ftxui::Color::Yellow;
        ftxui::Color cursor_line = ftxui::Color::RGB(38, 38, 48);
        ftxui::Color scroll_thumb = ftxui::Color::GrayLight;
        ftxui::Color scroll_track = ftxui::Color::GrayDark;

        // Syntax highlighting
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

        static Theme Dark()
        {
            return Theme{};
        }

        static Theme Light()
        {
            Theme t;
            t.heading1 = ftxui::Color::RGB(0x82, 0x50, 0xDF);
            t.heading2 = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.heading3 = ftxui::Color::RGB(0x1A, 0x7F, 0x37);
            t.bold_fg = ftxui::Color::Default;
            t.italic_fg = ftxui::Color::Default;
            t.code_fg = ftxui::Color::RGB(0x1F, 0x23, 0x28);
            t.code_bg = ftxui::Color::RGB(0xF6, 0xF8, 0xFA);
            t.math_fg = ftxui::Color::RGB(0x1A, 0x7F, 0x37);
            t.math_emphasis = ftxui::Color::RGB(0x2D, 0xA4, 0x4E);
            t.border = ftxui::Color::RGB(0xD0, 0xD7, 0xDE);
            t.truncation = ftxui::Color::Red;
            t.table_header = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.table_border = ftxui::Color::RGB(0xD0, 0xD7, 0xDE);
            t.blockquote = ftxui::Color::RGB(0x57, 0x60, 0x6A);
            t.list_marker = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.line_number = ftxui::Color::RGB(0x8C, 0x95, 0x9F);
            t.cursor_line_number = ftxui::Color::RGB(0x09, 0x69, 0xDA);
            t.cursor_line = ftxui::Color::RGB(0xEA, 0xEE, 0xF2);
            t.scroll_thumb = ftxui::Color::RGB(0x8C, 0x95, 0x9F);
            t.scroll_track = ftxui::Color::RGB(0xEA, 0xEE, 0xF2);

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

} // namespace ftxui::ext::md
