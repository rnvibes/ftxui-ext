#include "ftxui/ext/latex_math.h"
#include "ftxui/ext/md/parser.h"
#include <ftxui/dom/elements.hpp>

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int g_passed = 0;
int g_failed = 0;

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__               \
                      << "]: " #cond << std::endl;                             \
            ++g_failed;                                                        \
        } else {                                                               \
            ++g_passed;                                                        \
        }                                                                      \
    } while (0)

#define TEST_CASE(name)                                                        \
    std::cout << "Running test: " << name << "..." << std::endl;

} // namespace

void test_contains_and_find_math_span() {
    TEST_CASE("find_math_span and contains_math");

    // Plain text without math
    TEST_ASSERT(!ftxui::ext::contains_math("Hello world"));
    TEST_ASSERT(!ftxui::ext::find_math_span("Hello world", 0).has_value());

    // Escaped dollar
    TEST_ASSERT(!ftxui::ext::contains_math(R"(This is \$100 and not math)"));

    // Simple inline math
    {
        std::string_view s = "Energy is $E = mc^2$ today";
        TEST_ASSERT(ftxui::ext::contains_math(s));
        auto span = ftxui::ext::find_math_span(s, 0);
        TEST_ASSERT(span.has_value());
        TEST_ASSERT(span->first == 10);
        TEST_ASSERT(span->second == 20);
        TEST_ASSERT(s.substr(span->first, span->second - span->first) == "$E = mc^2$");
    }

    // Display math
    {
        std::string_view s = "Equation:\n$$\\int_0^1 x dx = \\frac{1}{2}$$\nDone";
        TEST_ASSERT(ftxui::ext::contains_math(s));
        auto span = ftxui::ext::find_math_span(s, 0);
        TEST_ASSERT(span.has_value());
        TEST_ASSERT(s.substr(span->first, span->second - span->first) ==
                    "$$\\int_0^1 x dx = \\frac{1}{2}$$");
    }

    // BUG FIX: Unmatched $ on line 1 should not hide math on line 2
    {
        std::string_view s = "I paid $5 at lunch.\nMath: $x + y = z$ is true.";
        TEST_ASSERT(ftxui::ext::contains_math(s));
        auto span = ftxui::ext::find_math_span(s, 0);
        TEST_ASSERT(span.has_value());
        TEST_ASSERT(s.substr(span->first, span->second - span->first) == "$x + y = z$");
    }

    // BUG FIX: Single $ closing must not match the 2nd $ of $$
    {
        std::string_view s = "Price $10 and then $$x = 1$$ is math";
        auto span = ftxui::ext::find_math_span(s, 0);
        TEST_ASSERT(span.has_value());
        // It should match $$x = 1$$, not "$10 and then $"
        TEST_ASSERT(s.substr(span->first, span->second - span->first) == "$$x = 1$$");
    }
}

void test_normalize_math_delimiters() {
    TEST_CASE("NormalizeMathDelimiters");

    // Inline \( ... \) -> $ ... $
    {
        std::string in = R"(Here is \( x^2 + y^2 = r^2 \) inline)";
        std::string out = ftxui::ext::NormalizeMathDelimiters(in);
        TEST_ASSERT(out.find('$') != std::string::npos);
        TEST_ASSERT(out.find("\\(") == std::string::npos);
    }

    // Display \[ ... \] -> $$ ... $$
    {
        std::string in = R"(Here is \[ x = \frac{-b \pm \sqrt{d}}{2a} \] display)";
        std::string out = ftxui::ext::NormalizeMathDelimiters(in);
        TEST_ASSERT(out.find("$$") != std::string::npos);
        TEST_ASSERT(out.find("\\[") == std::string::npos);
    }

    // Bare top-level \begin{pmatrix} ... \end{pmatrix} -> $$ \begin{pmatrix}...$$
    {
        std::string in = "\\begin{pmatrix}\n1 & 0 \\\\\n0 & 1\n\\end{pmatrix}";
        std::string out = ftxui::ext::NormalizeMathDelimiters(in);
        TEST_ASSERT(out.starts_with("$$"));
        TEST_ASSERT(out.ends_with("$$"));
    }

    // Code blocks are untouched
    {
        std::string in = "```\n\\[ not math \\]\n```";
        std::string out = ftxui::ext::NormalizeMathDelimiters(in);
        TEST_ASSERT(out == in);
    }
}

void test_substitute_inline_math() {
    TEST_CASE("substitute_inline_math");

    std::string in = "Alpha is $\\alpha$ and beta is $\\beta$.";
    std::string out = ftxui::ext::substitute_inline_math(in);
    TEST_ASSERT(out.find("α") != std::string::npos);
    TEST_ASSERT(out.find("β") != std::string::npos);
    TEST_ASSERT(out.find('$') == std::string::npos);

    // Vulgar fraction substitution
    std::string half = "Half is $\\frac{1}{2}$.";
    std::string out_half = ftxui::ext::substitute_inline_math(half);
    TEST_ASSERT(out_half.find("½") != std::string::npos);

    // Multiple unmatched $ before valid math
    std::string price = "Paid $10 on Mon and $20 on Tue. But $\\gamma = 1$.";
    std::string out_price = ftxui::ext::substitute_inline_math(price);
    TEST_ASSERT(out_price.find("γ = 1") != std::string::npos ||
                out_price.find("γ") != std::string::npos);
}

void test_render_math_inline_and_display() {
    TEST_CASE("render_math inline and display");

    auto dummy_style = ftxui::nothing;

    // Greek letters and operators
    auto e1 = ftxui::ext::render_math("\\alpha + \\beta = \\gamma", dummy_style,
                                       ftxui::ext::MathMode::Inline);
    TEST_ASSERT(e1 != nullptr);

    // Fractions in display vs inline
    auto frac_disp = ftxui::ext::render_math("\\frac{a + b}{c + d}", dummy_style,
                                             ftxui::ext::MathMode::Display);
    TEST_ASSERT(frac_disp != nullptr);

    auto frac_inline = ftxui::ext::render_math("\\frac{a + b}{c + d}", dummy_style,
                                               ftxui::ext::MathMode::Inline);
    TEST_ASSERT(frac_inline != nullptr);

    // BUG FIX: Root index preserved in display mode (\sqrt[3]{x})
    auto root_disp = ftxui::ext::render_math("\\sqrt[3]{x}", dummy_style,
                                             ftxui::ext::MathMode::Display);
    TEST_ASSERT(root_disp != nullptr);

    auto root_inline = ftxui::ext::render_math("\\sqrt[3]{x}", dummy_style,
                                               ftxui::ext::MathMode::Inline);
    TEST_ASSERT(root_inline != nullptr);

    // BUG FIX: Leading scripts are not dropped
    auto leading_sup = ftxui::ext::render_math("^{238}\\text{U}", dummy_style,
                                               ftxui::ext::MathMode::Inline);
    TEST_ASSERT(leading_sup != nullptr);

    auto leading_sub = ftxui::ext::render_math("_0^1 f(x) dx", dummy_style,
                                               ftxui::ext::MathMode::Inline);
    TEST_ASSERT(leading_sub != nullptr);

    // Matrices in display mode
    auto mat = ftxui::ext::render_math(
        "\\begin{pmatrix} 1 & 0 \\\\ 0 & 1 \\end{pmatrix}", dummy_style,
        ftxui::ext::MathMode::Display);
    TEST_ASSERT(mat != nullptr);

    // Cases environment
    auto cases_elem = ftxui::ext::render_math(
        "\\begin{cases} x & \\text{if } x > 0 \\\\ -x & \\text{otherwise} \\end{cases}",
        dummy_style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(cases_elem != nullptr);

    // Limits pragma
    auto lim = ftxui::ext::render_math("\\int\\limits_0^1 x dx", dummy_style,
                                       ftxui::ext::MathMode::Display);
    TEST_ASSERT(lim != nullptr);

    // Coloneqq symbol
    auto ceq = ftxui::ext::render_math("A \\coloneqq B", dummy_style,
                                       ftxui::ext::MathMode::Inline);
    TEST_ASSERT(ceq != nullptr);

    // Streaming recovery: unclosed group should not crash or return empty
    auto unclosed = ftxui::ext::render_math("\\frac{x + 1}{y + 2", dummy_style,
                                            ftxui::ext::MathMode::Display);
    TEST_ASSERT(unclosed != nullptr);

    // Null delimiter \left. ... \right|
    auto null_delim = ftxui::ext::render_math(
        "\\left. \\frac{df}{dx} \\right|_{x=0}", dummy_style,
        ftxui::ext::MathMode::Display);
    TEST_ASSERT(null_delim != nullptr);

    // Big operator with limits \sum_{n=1}^\infty
    auto sum_disp = ftxui::ext::render_math(
        "\\sum_{n=1}^\\infty \\frac{1}{n^2} = \\frac{\\pi^2}{6}", dummy_style,
        ftxui::ext::MathMode::Display);
    TEST_ASSERT(sum_disp != nullptr);

    // Accents
    auto accents_test = ftxui::ext::render_math(
        "\\hat{H}\\Psi = E\\Psi, \\quad \\dot{x} = v, \\quad \\vec{F} = m\\vec{a}",
        dummy_style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(accents_test != nullptr);

    // Math alphabets
    auto alphabets_test = ftxui::ext::render_math(
        "x \\in \\mathbb{R}^n, \\quad \\mathcal{L}_{\\text{SM}}, \\quad \\mathfrak{g}",
        dummy_style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(alphabets_test != nullptr);
}

void test_complex_katex_reference_formulas() {
    TEST_CASE("Complex KaTeX formulas from math.md");

    auto style = ftxui::nothing;

    // 1. Attention formula
    std::string_view attention =
        R"(\text{Attention}(Q, K, V) = \text{softmax}\left(\frac{QK^T}{\sqrt{d_k}}\right)V)";
    auto att_disp = ftxui::ext::render_math(attention, style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(att_disp != nullptr);
    auto att_inline = ftxui::ext::render_math(attention, style, ftxui::ext::MathMode::Inline);
    TEST_ASSERT(att_inline != nullptr);

    // 2. Time-dependent Schrödinger equation
    std::string_view schrodinger =
        R"(i\hbar \frac{\partial}{\partial t}\Psi(\mathbf{r}, t) = \left[ -\frac{\hbar^2}{2m}\nabla^2 + V(\mathbf{r}, t) \right] \Psi(\mathbf{r}, t))";
    auto sch_disp = ftxui::ext::render_math(schrodinger, style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(sch_disp != nullptr);

    // 3. Lorenz Attractor
    std::string_view lorenz =
        "\\begin{aligned}\n"
        "\\frac{dx}{dt} &= \\sigma (y - x) \\\\\n"
        "\\frac{dy}{dt} &= x(\\rho - z) - y \\\\\n"
        "\\frac{dz}{dt} &= xy - \\beta z\n"
        "\\end{aligned}";
    auto lor_disp = ftxui::ext::render_math(lorenz, style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(lor_disp != nullptr);

    // 4. Standard Model Lagrangian
    std::string_view sm =
        R"(\mathcal{L}_{\text{SM}} = -\frac{1}{4}G_{\mu\nu}^a G^{a\mu\nu} + \sum_\psi \bar{\psi}i\gamma^\mu D_\mu \psi)";
    auto sm_disp = ftxui::ext::render_math(sm, style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(sm_disp != nullptr);

    // 5. Einstein Field Equations with \coloneqq
    std::string_view einstein =
        R"(G_{\mu\nu} + \Lambda g_{\mu\nu} = \frac{8\pi G}{c^4} T_{\mu\nu}, \quad G_{\mu\nu} \coloneqq R_{\mu\nu} - \frac{1}{2}R g_{\mu\nu})";
    auto ein_disp = ftxui::ext::render_math(einstein, style, ftxui::ext::MathMode::Display);
    TEST_ASSERT(ein_disp != nullptr);
}


void test_next_glyph() {
    TEST_CASE("next_glyph");

    std::string_view utf8_text = "αβγ123";
    size_t pos = 0;
    std::string g1 = ftxui::ext::next_glyph(utf8_text, pos);
    TEST_ASSERT(g1 == "α");
    std::string g2 = ftxui::ext::next_glyph(utf8_text, pos);
    TEST_ASSERT(g2 == "β");
    std::string g3 = ftxui::ext::next_glyph(utf8_text, pos);
    TEST_ASSERT(g3 == "γ");
    std::string g4 = ftxui::ext::next_glyph(utf8_text, pos);
    TEST_ASSERT(g4 == "1");

    // Out of bounds safety
    size_t eof_pos = utf8_text.size();
    std::string geof = ftxui::ext::next_glyph(utf8_text, eof_pos);
    TEST_ASSERT(geof.empty());
}

void test_bold_math_interaction() {
    TEST_CASE("Bold and math interaction (preventing dangling **)");

    // 1. Exact user case: bold sentence wrapping an inline math span \( \phi \)
    {
        std::string raw =
            "**In summary, the equation states that the total second-order derivative of the field ( \\(\\phi\\) ) with respect to spacetime coordinates must equal zero when combined with the mass term.**";
        auto doc = ftxui::ext::parse_markdown(raw);
        TEST_ASSERT(!doc.empty());
        TEST_ASSERT(doc[0].kind == ftxui::ext::MdBlockKind::Paragraph);
        const auto &spans = doc[0].spans;

        // Verify that NO span contains the raw '**' markers
        for (const auto &span : spans) {
            TEST_ASSERT(span.text.find("**") == std::string::npos);
        }

        // Expected: [Bold, Math(emphasized=true), Bold]
        TEST_ASSERT(spans.size() == 3);
        TEST_ASSERT(spans[0].kind == ftxui::ext::MdInlineKind::Bold);
        TEST_ASSERT(spans[0].text.starts_with("In summary"));
        TEST_ASSERT(spans[1].kind == ftxui::ext::MdInlineKind::Math);
        TEST_ASSERT(spans[1].emphasized == true);
        TEST_ASSERT(spans[1].text == "\\phi");
        TEST_ASSERT(spans[2].kind == ftxui::ext::MdInlineKind::Bold);
        TEST_ASSERT(spans[2].text.ends_with("mass term."));
    }

    // 2. Dollar syntax: **$x$ + $y$ = $z$**
    {
        std::string raw = "**Let $x$ and $y$ be variables.**";
        auto doc = ftxui::ext::parse_markdown(raw);
        TEST_ASSERT(!doc.empty());
        const auto &spans = doc[0].spans;
        for (const auto &span : spans) {
            TEST_ASSERT(span.text.find("**") == std::string::npos);
        }
        TEST_ASSERT(spans.size() == 5);
        TEST_ASSERT(spans[0].kind == ftxui::ext::MdInlineKind::Bold);
        TEST_ASSERT(spans[0].text == "Let ");
        TEST_ASSERT(spans[1].kind == ftxui::ext::MdInlineKind::Math);
        TEST_ASSERT(spans[1].emphasized == true);
        TEST_ASSERT(spans[1].text == "x");
        TEST_ASSERT(spans[2].kind == ftxui::ext::MdInlineKind::Bold);
        TEST_ASSERT(spans[2].text == " and ");
        TEST_ASSERT(spans[3].kind == ftxui::ext::MdInlineKind::Math);
        TEST_ASSERT(spans[3].emphasized == true);
        TEST_ASSERT(spans[3].text == "y");
        TEST_ASSERT(spans[4].kind == ftxui::ext::MdInlineKind::Bold);
        TEST_ASSERT(spans[4].text == " be variables.");
    }

    // 3. Immediately wrapped: **$E=mc^2$**
    {
        std::string raw = "**$E=mc^2$**";
        auto doc = ftxui::ext::parse_markdown(raw);
        TEST_ASSERT(!doc.empty());
        const auto &spans = doc[0].spans;
        TEST_ASSERT(spans.size() == 1);
        TEST_ASSERT(spans[0].kind == ftxui::ext::MdInlineKind::Math);
        TEST_ASSERT(spans[0].emphasized == true);
        TEST_ASSERT(spans[0].text == "E=mc^2");
    }

    // 4. Italic wrapping math: *Note: $x > 0$ holds.*
    {
        std::string raw = "*Note: $x > 0$ holds.*";
        auto doc = ftxui::ext::parse_markdown(raw);
        TEST_ASSERT(!doc.empty());
        const auto &spans = doc[0].spans;
        for (const auto &span : spans) {
            TEST_ASSERT(span.text.find('*') == std::string::npos);
        }
        TEST_ASSERT(spans.size() == 3);
        TEST_ASSERT(spans[0].kind == ftxui::ext::MdInlineKind::Italic);
        TEST_ASSERT(spans[1].kind == ftxui::ext::MdInlineKind::Math);
        TEST_ASSERT(spans[1].emphasized == true);
        TEST_ASSERT(spans[2].kind == ftxui::ext::MdInlineKind::Italic);
    }
}

int main() {
    std::cout << "Starting latex_math tests..." << std::endl;

    test_contains_and_find_math_span();
    test_normalize_math_delimiters();
    test_substitute_inline_math();
    test_render_math_inline_and_display();
    test_complex_katex_reference_formulas();
    test_next_glyph();
    test_bold_math_interaction();

    std::cout << "\n===============================" << std::endl;
    std::cout << "Tests passed: " << g_passed << std::endl;
    std::cout << "Tests failed: " << g_failed << std::endl;
    std::cout << "===============================" << std::endl;

    return g_failed == 0 ? 0 : 1;
}
