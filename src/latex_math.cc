#include "ftxui/ext/latex_math.h"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ftxui::ext
{

    namespace
    {

        // ===================================================================
        // 1. DATA-DRIVEN POD TYPOGRAPHY & SYMBOL LOOKUP TABLES
        // ===================================================================

        // Key-value pair for zero-allocation constexpr symbol tables stored in .rodata.
        struct SymbolEntry
        {
            std::string_view name;
            std::string_view glyph;
        };

        // Binary search lookup in a sorted SymbolEntry table.
        constexpr const SymbolEntry *lookup_symbol(std::span<const SymbolEntry> table,
                                                   std::string_view key)
        {
            auto it = std::lower_bound(
                table.begin(), table.end(), key,
                [](const SymbolEntry &entry, std::string_view k)
                {
                    return entry.name < k;
                });
            if (it != table.end() && it->name == key)
                return &(*it);
            return nullptr;
        }

        // Binary search set membership in a sorted std::string_view table.
        constexpr bool contains_key(std::span<const std::string_view> table,
                                    std::string_view key)
        {
            return std::binary_search(table.begin(), table.end(), key);
        }

        // Precomposed vulgar fractions for single-character inline denominators.
        constexpr SymbolEntry kVulgarFractions[] = {
            {"1/2", "½"},
            {"1/3", "⅓"},
            {"1/4", "¼"},
            {"1/5", "⅕"},
            {"1/6", "⅙"},
            {"1/8", "⅛"},
            {"2/3", "⅔"},
            {"2/5", "⅖"},
            {"3/4", "¾"},
            {"3/5", "⅗"},
            {"3/8", "⅜"},
            {"4/5", "⅘"},
            {"5/6", "⅚"},
            {"5/8", "⅝"},
            {"7/8", "⅞"},
        };

        // Unicode superscripts.
        constexpr SymbolEntry kSuperScripts[] = {
            {"(", "⁽"},
            {")", "⁾"},
            {"*", "·"},
            {"+", "⁺"},
            {"-", "⁻"},
            {".", "·"},
            {"/", "ᐟ"},
            {"0", "⁰"},
            {"1", "¹"},
            {"2", "²"},
            {"3", "³"},
            {"4", "⁴"},
            {"5", "⁵"},
            {"6", "⁶"},
            {"7", "⁷"},
            {"8", "⁸"},
            {"9", "⁹"},
            {"=", "⁼"},
            {"a", "ᵃ"},
            {"alpha", "ᵅ"},
            {"b", "ᵇ"},
            {"c", "ᶜ"},
            {"chi", "ᵡ"},
            {"d", "ᵈ"},
            {"delta", "ᵟ"},
            {"e", "ᵉ"},
            {"f", "ᶠ"},
            {"g", "ᵍ"},
            {"gamma", "ᵞ"},
            {"h", "ʰ"},
            {"i", "ⁱ"},
            {"k", "ᵏ"},
            {"l", "ˡ"},
            {"m", "ᵐ"},
            {"n", "ⁿ"},
            {"p", "ᵖ"},
            {"phi", "ᶲ"},
            {"r", "ʳ"},
            {"s", "ˢ"},
            {"t", "ᵗ"},
            {"theta", "ᶿ"},
            {"u", "ᵘ"},
            {"v", "ᵛ"},
            {"w", "ʷ"},
            {"x", "ˣ"},
            {"y", "ʸ"},
            {"z", "ᶻ"},
            {"α", "ᵅ"},
            {"β", "ᵝ"},
            {"γ", "ᵞ"},
            {"δ", "ᵟ"},
            {"θ", "ᶿ"},
            {"φ", "ᶲ"},
            {"χ", "ᵡ"},
        };

        // Unicode subscripts.
        constexpr SymbolEntry kSubScripts[] = {
            {"(", "₍"},
            {")", "₎"},
            {"+", "₊"},
            {"-", "₋"},
            {".", "․"},
            {"/", "⸝"},
            {"0", "₀"},
            {"1", "₁"},
            {"2", "₂"},
            {"3", "₃"},
            {"4", "₄"},
            {"5", "₅"},
            {"6", "₆"},
            {"7", "₇"},
            {"8", "₈"},
            {"9", "₉"},
            {"=", "₌"},
            {"a", "ₐ"},
            {"chi", "ᵪ"},
            {"e", "ₑ"},
            {"h", "ₕ"},
            {"i", "ᵢ"},
            {"j", "ⱼ"},
            {"k", "ₖ"},
            {"l", "ₗ"},
            {"m", "ₘ"},
            {"n", "ₙ"},
            {"o", "ₒ"},
            {"p", "ₚ"},
            {"phi", "ᵩ"},
            {"r", "ᵣ"},
            {"rho", "ᵨ"},
            {"s", "ₛ"},
            {"t", "ₜ"},
            {"u", "ᵤ"},
            {"v", "ᵥ"},
            {"x", "ₓ"},
            {"ρ", "ᵨ"},
            {"φ", "ᵩ"},
            {"χ", "ᵪ"},
        };

        // Greek letters, standard functions, relations, operators, and arrows.
        constexpr SymbolEntry kMathSymbols[] = {
            {"Alpha", "Α"},
            {"Beta", "Β"},
            {"Chi", "Χ"},
            {"Delta", "Δ"},
            {"Downarrow", "⇓"},
            {"Epsilon", "Ε"},
            {"Eta", "Η"},
            {"Gamma", "Γ"},
            {"Im", "ℑ"},
            {"Iota", "Ι"},
            {"Kappa", "Κ"},
            {"Lambda", "Λ"},
            {"Leftarrow", "⇐"},
            {"Leftrightarrow", "⇔"},
            {"Mu", "Μ"},
            {"Nu", "Ν"},
            {"Omega", "Ω"},
            {"Phi", "Φ"},
            {"Pi", "Π"},
            {"Pr", "Pr"},
            {"Psi", "Ψ"},
            {"Re", "ℜ"},
            {"Rho", "Ρ"},
            {"Rightarrow", "⇒"},
            {"Sigma", "Σ"},
            {"Tau", "Τ"},
            {"Theta", "Θ"},
            {"Uparrow", "⇑"},
            {"Upsilon", "Υ"},
            {"Vert", "‖"},
            {"Xi", "Ξ"},
            {"Zeta", "Ζ"},
            {"aleph", "ℵ"},
            {"alpha", "α"},
            {"angle", "∠"},
            {"approx", "≈"},
            {"arccos", "arccos"},
            {"arcsin", "arcsin"},
            {"arctan", "arctan"},
            {"arg", "arg"},
            {"ast", "*"},
            {"because", "∵"},
            {"beta", "β"},
            {"beth", "ℶ"},
            {"bigcap", "⋂"},
            {"bigcirc", "◯"},
            {"bigcup", "⋃"},
            {"bigodot", "⨀"},
            {"bigoplus", "⨁"},
            {"bigotimes", "⨂"},
            {"bigsqcup", "⨆"},
            {"bigvee", "⋁"},
            {"bigwedge", "⋀"},
            {"blacksquare", "■"},
            {"bot", "⊥"},
            {"bullet", "•"},
            {"cap", "∩"},
            {"cdot", "·"},
            {"cdots", "⋯"},
            {"checkmark", "✓"},
            {"chi", "χ"},
            {"circ", "∘"},
            {"coloneq", "≔"},
            {"coloneqq", "≔"},
            {"cong", "≅"},
            {"coprod", "∐"},
            {"cos", "cos"},
            {"cosh", "cosh"},
            {"cot", "cot"},
            {"coth", "coth"},
            {"csc", "csc"},
            {"csch", "csch"},
            {"cup", "∪"},
            {"dagger", "†"},
            {"dashv", "⊣"},
            {"ddagger", "‡"},
            {"ddots", "⋱"},
            {"deg", "deg"},
            {"degree", "°"},
            {"delta", "δ"},
            {"det", "det"},
            {"diamond", "◇"},
            {"dim", "dim"},
            {"div", "÷"},
            {"dots", "…"},
            {"downarrow", "↓"},
            {"ell", "ℓ"},
            {"emptyset", "∅"},
            {"epsilon", "ϵ"},
            {"equiv", "≡"},
            {"eta", "η"},
            {"eth", "ð"},
            {"exists", "∃"},
            {"exp", "exp"},
            {"forall", "∀"},
            {"gamma", "γ"},
            {"gcd", "gcd"},
            {"ge", "≥"},
            {"geq", "≥"},
            {"gets", "←"},
            {"gg", "≫"},
            {"gimel", "ℷ"},
            {"gt", ">"},
            {"hbar", "ħ"},
            {"hom", "hom"},
            {"hookleftarrow", "↩"},
            {"hookrightarrow", "↪"},
            {"hslash", "ℏ"},
            {"iddots", "⋰"},
            {"iff", "⟺"},
            {"imath", "ı"},
            {"impliedby", "⟸"},
            {"implies", "⟹"},
            {"in", "∈"},
            {"inf", "inf"},
            {"infty", "∞"},
            {"int", "∫"},
            {"iota", "ι"},
            {"jmath", "ȷ"},
            {"kappa", "κ"},
            {"ker", "ker"},
            {"lambda", "λ"},
            {"land", "∧"},
            {"langle", "⟨"},
            {"lceil", "⌈"},
            {"ldots", "…"},
            {"le", "≤"},
            {"leftarrow", "←"},
            {"leftharpoonup", "↼"},
            {"leftrightarrow", "↔"},
            {"leq", "≤"},
            {"lfloor", "⌊"},
            {"lg", "lg"},
            {"lim", "lim"},
            {"liminf", "lim inf"},
            {"limsup", "lim sup"},
            {"ll", "≪"},
            {"ln", "ln"},
            {"lnot", "¬"},
            {"log", "log"},
            {"longleftarrow", "⟵"},
            {"longmapsto", "⟼"},
            {"longrightarrow", "⟶"},
            {"lor", "∨"},
            {"lt", "<"},
            {"mapsto", "↦"},
            {"mathbbC", "ℂ"},
            {"mathbbH", "ℍ"},
            {"mathbbN", "ℕ"},
            {"mathbbP", "ℙ"},
            {"mathbbQ", "ℚ"},
            {"mathbbR", "ℝ"},
            {"mathbbZ", "ℤ"},
            {"max", "max"},
            {"measuredangle", "∡"},
            {"mid", "|"},
            {"min", "min"},
            {"models", "⊨"},
            {"mp", "∓"},
            {"mu", "μ"},
            {"nabla", "∇"},
            {"ne", "≠"},
            {"nearrow", "↗"},
            {"neg", "¬"},
            {"neq", "≠"},
            {"nexist", "∄"},
            {"nexists", "∄"},
            {"ni", "∋"},
            {"notin", "∉"},
            {"nparallel", "∦"},
            {"nu", "ν"},
            {"nwarrow", "↖"},
            {"odot", "⊙"},
            {"omega", "ω"},
            {"ominus", "⊖"},
            {"oplus", "⊕"},
            {"oslash", "⊘"},
            {"otimes", "⊗"},
            {"parallel", "∥"},
            {"partial", "∂"},
            {"perp", "⊥"},
            {"phi", "φ"},
            {"pi", "π"},
            {"pm", "±"},
            {"prec", "≺"},
            {"preceq", "⪯"},
            {"prime", "′"},
            {"prod", "∏"},
            {"propto", "∝"},
            {"psi", "ψ"},
            {"rangle", "⟩"},
            {"rceil", "⌉"},
            {"rfloor", "⌋"},
            {"rho", "ρ"},
            {"rightarrow", "→"},
            {"rightharpoonup", "⇀"},
            {"rightleftharpoons", "⇌"},
            {"searrow", "↘"},
            {"sec", "sec"},
            {"sech", "sech"},
            {"setminus", "∖"},
            {"sigma", "σ"},
            {"sim", "∼"},
            {"simeq", "≃"},
            {"sin", "sin"},
            {"sinh", "sinh"},
            {"sqcap", "⊓"},
            {"sqcup", "⊔"},
            {"sqsubseteq", "⊑"},
            {"sqsupseteq", "⊒"},
            {"square", "□"},
            {"star", "⋆"},
            {"subset", "⊂"},
            {"subseteq", "⊆"},
            {"succ", "≻"},
            {"succeq", "⪰"},
            {"sum", "∑"},
            {"sup", "sup"},
            {"supset", "⊃"},
            {"supseteq", "⊇"},
            {"swarrow", "↙"},
            {"tan", "tan"},
            {"tanh", "tanh"},
            {"tau", "τ"},
            {"therefore", "∴"},
            {"theta", "θ"},
            {"times", "×"},
            {"to", "→"},
            {"top", "⊤"},
            {"triangle", "△"},
            {"triangleleft", "◁"},
            {"triangleright", "▷"},
            {"uparrow", "↑"},
            {"upsilon", "υ"},
            {"vDash", "⊨"},
            {"varepsilon", "ε"},
            {"varnothing", "∅"},
            {"varphi", "ϕ"},
            {"varpi", "ϖ"},
            {"varrho", "ϱ"},
            {"varsigma", "ς"},
            {"vartheta", "ϑ"},
            {"vdash", "⊢"},
            {"vdots", "⋮"},
            {"vee", "∨"},
            {"wedge", "∧"},
            {"wp", "℘"},
            {"xi", "ξ"},
            {"zeta", "ζ"},
        };

        // Binary and relation operators that receive surrounding spaces in math mode.
        constexpr std::string_view kSpacedOperators[] = {
            "+",
            "-",
            "<",
            "=",
            ">",
            "±",
            "×",
            "÷",
            "←",
            "→",
            "↔",
            "↦",
            "⇌",
            "⇐",
            "⇒",
            "⇔",
            "∈",
            "∉",
            "∋",
            "∓",
            "∖",
            "∝",
            "∧",
            "∨",
            "∩",
            "∪",
            "∴",
            "∵",
            "∼",
            "≃",
            "≅",
            "≈",
            "≔",
            "≠",
            "≡",
            "≤",
            "≥",
            "≪",
            "≫",
            "≺",
            "≻",
            "⊂",
            "⊃",
            "⊆",
            "⊇",
            "⊓",
            "⊔",
            "⊕",
            "⊗",
            "⊢",
            "⊣",
            "⊨",
            "⋅",
            "⟵",
            "⟶",
            "⟸",
            "⟹",
            "⟺",
            "⪯",
            "⪰",
        };

        // Operators that stack their limits above and below in display style.
        constexpr std::string_view kBigOperators[] = {
            "inf",
            "lim",
            "max",
            "min",
            "sup",
            "∏",
            "∐",
            "∑",
            "∫",
            "∬",
            "∭",
            "∮",
            "⋂",
            "⋃",
            "⨁",
            "⨂",
            "⨆",
        };

        // Named delimiter macros parsed by read_delim_token.
        constexpr SymbolEntry kNamedDelimiters[] = {
            {"Vert", "‖"},
            {"backslash", "\\"},
            {"langle", "⟨"},
            {"lbrace", "{"},
            {"lbrack", "["},
            {"lceil", "⌈"},
            {"lfloor", "⌊"},
            {"rangle", "⟩"},
            {"rbrace", "}"},
            {"rbrack", "]"},
            {"rceil", "⌉"},
            {"rfloor", "⌋"},
            {"vert", "|"},
        };

        // Combining accent characters for \hat, \dot, \vec, etc.
        constexpr SymbolEntry kAccents[] = {
            {"acute", "\u0301"},
            {"bar", "\u0304"},
            {"breve", "\u0306"},
            {"check", "\u030c"},
            {"ddot", "\u0308"},
            {"dot", "\u0307"},
            {"grave", "\u0300"},
            {"hat", "\u0302"},
            {"mathring", "\u030a"},
            {"overleftarrow", "\u20d6"},
            {"overrightarrow", "\u20d7"},
            {"tilde", "\u0303"},
            {"vec", "\u20d7"},
            {"widehat", "\u0302"},
            {"widetilde", "\u0303"},
        };

        // Extensible delimiter bracket piece configuration.
        struct DelimiterExt
        {
            std::string_view delim;
            const char *top;
            const char *mid;
            const char *bottom;
            const char *hook;
        };

        constexpr DelimiterExt kExtDelimiters[] = {
            {"(", "⎛", "⎜", "⎝", nullptr},
            {")", "⎞", "⎟", "⎠", nullptr},
            {"[", "⎡", "⎢", "⎣", nullptr},
            {"]", "⎤", "⎥", "⎦", nullptr},
            {"{", "⎧", "⎪", "⎩", "⎨"},
            {"|", "│", "│", "│", nullptr},
            {"}", "⎫", "⎪", "⎭", "⎬"},
            {"‖", "║", "║", "║", nullptr},
            {"⌈", "⌈", "⎢", "⎢", nullptr},
            {"⌉", "⌉", "⎥", "⎥", nullptr},
            {"⌊", "⎢", "⎢", "⌊", nullptr},
            {"⌋", "⎥", "⎥", "⌋", nullptr},
        };

        // Environment column alignment styles.
        enum class ColumnStyle
        {
            Centred,    // matrices
            AlignPairs, // align family: even columns right-aligned, odd left
            LeftAll,    // cases
            Spec,       // array{lcr}
        };

        struct EnvTraits
        {
            std::string_view name;
            std::string_view left_delim = ".";
            std::string_view right_delim = ".";
            ColumnStyle columns = ColumnStyle::Centred;
            bool wide_gap = false;
        };

        constexpr EnvTraits kEnvTraits[] = {
            {"Bmatrix", "{", "}", ColumnStyle::Centred, false},
            {"Vmatrix", "‖", "‖", ColumnStyle::Centred, false},
            {"align", ".", ".", ColumnStyle::AlignPairs, false},
            {"alignat", ".", ".", ColumnStyle::AlignPairs, false},
            {"aligned", ".", ".", ColumnStyle::AlignPairs, false},
            {"array", ".", ".", ColumnStyle::Spec, false},
            {"bmatrix", "[", "]", ColumnStyle::Centred, false},
            {"cases", "{", ".", ColumnStyle::LeftAll, true},
            {"eqnarray", ".", ".", ColumnStyle::AlignPairs, false},
            {"flalign", ".", ".", ColumnStyle::AlignPairs, false},
            {"gather", ".", ".", ColumnStyle::Centred, false},
            {"gathered", ".", ".", ColumnStyle::Centred, false},
            {"matrix", ".", ".", ColumnStyle::Centred, false},
            {"pmatrix", "(", ")", ColumnStyle::Centred, false},
            {"smallmatrix", ".", ".", ColumnStyle::Centred, false},
            {"split", ".", ".", ColumnStyle::AlignPairs, false},
            {"vmatrix", "|", "|", ColumnStyle::Centred, false},
        };

        std::string_view trim_spaces_view(std::string_view text)
        {
            const size_t begin = text.find_first_not_of(' ');
            if (begin == std::string_view::npos)
                return {};
            return text.substr(begin, text.find_last_not_of(' ') - begin + 1);
        }

        std::string trim_spaces(std::string_view text)
        {
            return std::string(trim_spaces_view(text));
        }

        bool is_spaced_operator(std::string_view glyph)
        {
            return contains_key(kSpacedOperators, glyph);
        }

        bool is_big_operator(std::string_view text)
        {
            return contains_key(kBigOperators, trim_spaces_view(text));
        }

        const EnvTraits *lookup_env_traits(std::string_view name)
        {
            auto it = std::lower_bound(
                std::begin(kEnvTraits), std::end(kEnvTraits), name,
                [](const EnvTraits &traits, std::string_view k)
                {
                    return traits.name < k;
                });
            if (it != std::end(kEnvTraits) && it->name == name)
                return &(*it);
            return nullptr;
        }

        bool is_known_math_environment(std::string_view name)
        {
            std::string_view base = name;
            if (!base.empty() && base.back() == '*')
                base.remove_suffix(1);
            return lookup_env_traits(base) != nullptr || base == "equation" ||
                   base == "displaymath";
        }

        void append_codepoint(std::string &out, uint32_t cp)
        {
            if (cp < 0x80)
                out += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        enum class MathAlphabet
        {
            DoubleStruck, // \mathbb
            Fraktur,      // \mathfrak
            Script,       // \mathcal, \mathscr
        };

        std::string map_math_alphabet(std::string_view body, MathAlphabet which)
        {
            std::string out;
            out.reserve(body.size() * 4);
            for (char ch : body)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                uint32_t cp = 0;
                switch (which)
                {
                case MathAlphabet::DoubleStruck:
                    switch (c)
                    {
                    case 'C': cp = 0x2102; break;
                    case 'H': cp = 0x210D; break;
                    case 'N': cp = 0x2115; break;
                    case 'P': cp = 0x2119; break;
                    case 'Q': cp = 0x211A; break;
                    case 'R': cp = 0x211D; break;
                    case 'Z': cp = 0x2124; break;
                    default:
                        if (c >= 'A' && c <= 'Z')
                            cp = 0x1D538 + (c - 'A');
                        else if (c >= 'a' && c <= 'z')
                            cp = 0x1D552 + (c - 'a');
                        else if (c >= '0' && c <= '9')
                            cp = 0x1D7D8 + (c - '0');
                        break;
                    }
                    break;
                case MathAlphabet::Fraktur:
                    switch (c)
                    {
                    case 'C': cp = 0x212D; break;
                    case 'H': cp = 0x210C; break;
                    case 'I': cp = 0x2111; break;
                    case 'R': cp = 0x211C; break;
                    case 'Z': cp = 0x2128; break;
                    default:
                        if (c >= 'A' && c <= 'Z')
                            cp = 0x1D504 + (c - 'A');
                        else if (c >= 'a' && c <= 'z')
                            cp = 0x1D51E + (c - 'a');
                        break;
                    }
                    break;
                case MathAlphabet::Script:
                    switch (c)
                    {
                    case 'B': cp = 0x212C; break;
                    case 'E': cp = 0x2130; break;
                    case 'F': cp = 0x2131; break;
                    case 'H': cp = 0x210B; break;
                    case 'I': cp = 0x2110; break;
                    case 'L': cp = 0x2112; break;
                    case 'M': cp = 0x2133; break;
                    case 'R': cp = 0x211B; break;
                    case 'e': cp = 0x212F; break;
                    case 'g': cp = 0x210A; break;
                    case 'o': cp = 0x2134; break;
                    default:
                        if (c >= 'A' && c <= 'Z')
                            cp = 0x1D49C + (c - 'A');
                        else if (c >= 'a' && c <= 'z')
                            cp = 0x1D4B6 + (c - 'a');
                        break;
                    }
                    break;
                }
                if (cp)
                    append_codepoint(out, cp);
                else
                    out += ch;
            }
            return out;
        }

        // ===================================================================
        // 2. TERMINAL 2D LAYOUT & BOX MODEL
        // ===================================================================

        // 2D Character-grid layout block with baseline height awareness.
        struct LayoutBox
        {
            std::vector<std::string> rows;
            int height_above = 0;
            int height_below = 0;

            int width() const
            {
                int result = 0;
                for (const auto &row : rows)
                    result = std::max(result, ftxui::string_width(row));
                return result;
            }

            int total_height() const { return height_above + height_below + 1; }
            bool is_normalized() const
            {
                return static_cast<int>(rows.size()) == total_height();
            }

            void normalize()
            {
                const int have = static_cast<int>(rows.size());
                const int declared = total_height();
                if (have == declared)
                    return;
                if (have < declared)
                    rows.resize(static_cast<size_t>(declared));
                else
                    height_below = have - 1 - height_above;
            }
        };

        LayoutBox math_text(std::string text)
        {
            if (text.empty())
                return {{{""}}, 0, 0};
            return {{{std::move(text)}}, 0, 0};
        }

        std::string rule(int count, std::string_view glyph = "─")
        {
            std::string out;
            out.reserve(static_cast<size_t>(std::max(0, count)) * glyph.size());
            for (int i = 0; i < std::max(0, count); ++i)
                out += glyph;
            return out;
        }

        std::string spaces(int count)
        {
            return std::string(static_cast<size_t>(std::max(0, count)), ' ');
        }

        // Flatten a box to a single space-separated line.
        std::string flatten_box(const LayoutBox &box)
        {
            std::string result;
            for (const auto &row : box.rows)
            {
                if (!result.empty() && !result.ends_with(' ') && !row.empty())
                    result += ' ';
                result += row;
            }
            return result;
        }

        // Combine a span of boxes horizontally in linear time with zero redundant decodes.
        LayoutBox math_hrow(std::span<const LayoutBox> pieces)
        {
            if (pieces.empty())
                return math_text("");
            if (pieces.size() == 1 && pieces[0].is_normalized())
                return pieces[0];

            int top = 0, bottom = 0;
            for (const auto &piece : pieces)
            {
                top = std::max(top, piece.height_above);
                bottom = std::max(bottom, piece.height_below);
            }
            const int height = top + 1 + bottom;

            LayoutBox result;
            result.height_above = top;
            result.height_below = bottom;
            result.rows.assign(static_cast<size_t>(height), std::string());

            std::vector<int> row_width(static_cast<size_t>(height), 0);
            int placed_width = 0;

            for (const auto &piece : pieces)
            {
                const int piece_width = piece.width();
                const int row_offset = top - piece.height_above;
                for (int y = 0; y < height; ++y)
                {
                    const int piece_y = y - row_offset;
                    if (piece_y < 0 || piece_y >= static_cast<int>(piece.rows.size()))
                        continue;
                    const std::string &source = piece.rows[static_cast<size_t>(piece_y)];
                    if (source.empty())
                        continue;
                    auto &row = result.rows[static_cast<size_t>(y)];
                    row += spaces(placed_width - row_width[static_cast<size_t>(y)]);
                    row += source;
                    row_width[static_cast<size_t>(y)] =
                        placed_width + ftxui::string_width(source);
                }
                placed_width += piece_width;
            }
            return result;
        }

        // Combine two boxes horizontally.
        LayoutBox math_hbox(const LayoutBox &left, const LayoutBox &right)
        {
            const LayoutBox pair[2] = {left, right};
            return math_hrow(pair);
        }

        // An operand needs parens in "a/b" notation unless it is a single atom.
        bool needs_parens(std::string_view operand)
        {
            if (operand.empty())
                return false;
            if (operand.find_first_of(" +-*/=<>^_") != std::string_view::npos)
                return true;
            size_t pos = 0, glyphs = 0;
            while (pos < operand.size())
            {
                next_glyph(operand, pos);
                if (++glyphs > 1)
                    return true;
            }
            return false;
        }

        // Single-row "a/b", preferring a precomposed vulgar fraction when available.
        LayoutBox linear_fraction(const LayoutBox &numerator,
                                  const LayoutBox &denominator)
        {
            const std::string top = trim_spaces(flatten_box(numerator));
            const std::string bottom = trim_spaces(flatten_box(denominator));
            if (top.size() == 1 && bottom.size() == 1)
            {
                char key[3] = {top[0], '/', bottom[0]};
                if (const auto *entry = lookup_symbol(kVulgarFractions, std::string_view(key, 3)))
                    return math_text(std::string(entry->glyph));
            }
            const std::string left = needs_parens(top) ? "(" + top + ")" : top;
            const std::string right = needs_parens(bottom) ? "(" + bottom + ")" : bottom;
            return math_text(left + "/" + right);
        }

        LayoutBox math_fraction(const LayoutBox &numerator,
                                const LayoutBox &denominator,
                                MathMode mode)
        {
            if (mode == MathMode::Inline)
                return linear_fraction(numerator, denominator);
            const int content_width = std::max(numerator.width(), denominator.width());
            const int width = content_width + 2;
            LayoutBox result;
            result.height_above = static_cast<int>(numerator.rows.size());
            result.height_below = static_cast<int>(denominator.rows.size());
            result.rows.reserve(numerator.rows.size() + denominator.rows.size() + 1);

            auto add_centered = [&](const LayoutBox &part)
            {
                const int padding = (width - part.width()) / 2;
                for (const auto &row : part.rows)
                    result.rows.push_back(spaces(padding) + row);
            };
            add_centered(numerator);
            result.rows.push_back(rule(std::max(1, width)));
            add_centered(denominator);

            return result;
        }

        LayoutBox math_integral(int height, std::string_view symbol = "int")
        {
            const std::string single_sym = symbol == "oint"   ? "∮"
                                           : symbol == "iint" ? "∬"
                                           : symbol == "iiint" ? "∭"
                                                              : "∫";
            if (height <= 1)
                return math_text(single_sym);
            LayoutBox result;
            result.height_above = height / 2;
            result.height_below = height - 1 - result.height_above;
            result.rows.reserve(static_cast<size_t>(height));
            result.rows.push_back("⌠");
            for (int i = 1; i < height - 1; ++i)
                result.rows.push_back("│");
            result.rows.push_back("⌡");
            return result;
        }

        // Centers `script` above or below `base`.
        LayoutBox stack_limit(const LayoutBox &base, const LayoutBox &script, bool above)
        {
            const bool flat_script = script.rows.size() <= 1;
            const std::string flat =
                flat_script ? trim_spaces(flatten_box(script)) : std::string();
            const int script_width =
                flat_script ? ftxui::string_width(flat) : script.width();
            const int script_height =
                flat_script ? 1 : static_cast<int>(script.rows.size());
            const int width = std::max(base.width(), script_width);
            auto centred = [&](const std::string &row)
            {
                const int pad = (width - ftxui::string_width(row)) / 2;
                return spaces(pad) + row +
                       spaces(width - pad - ftxui::string_width(row));
            };
            auto add_script = [&](LayoutBox &out)
            {
                if (flat_script)
                    out.rows.push_back(centred(flat));
                else
                    for (const auto &row : script.rows)
                        out.rows.push_back(centred(row));
            };
            LayoutBox out;
            out.height_above = base.height_above + (above ? script_height : 0);
            out.height_below = base.height_below + (above ? 0 : script_height);
            if (above)
                add_script(out);
            for (const auto &row : base.rows)
                out.rows.push_back(centred(row));
            if (!above)
                add_script(out);
            return out;
        }

        // Delimiter stretched over height rows.
        LayoutBox make_delimiter_column(std::string_view delim, int height_above, int height_below)
        {
            const int height = height_above + 1 + height_below;
            LayoutBox out;
            out.height_above = height_above;
            out.height_below = height_below;
            if (delim.empty() || delim == ".")
            {
                out.rows.assign(static_cast<size_t>(height), "");
                return out;
            }
            if (height <= 1)
            {
                out.rows.push_back(std::string(delim));
                return out;
            }

            auto it = std::lower_bound(
                std::begin(kExtDelimiters), std::end(kExtDelimiters), delim,
                [](const DelimiterExt &d, std::string_view k)
                {
                    return d.delim < k;
                });

            if (it == std::end(kExtDelimiters) || it->delim != delim)
            {
                out.rows.assign(static_cast<size_t>(height), " ");
                out.rows[static_cast<size_t>(height_above)] = std::string(delim);
                return out;
            }

            const auto &ext = *it;
            out.rows.reserve(static_cast<size_t>(height));
            for (int y = 0; y < height; ++y)
            {
                if (y == 0)
                    out.rows.push_back(ext.top);
                else if (y == height - 1)
                    out.rows.push_back(ext.bottom);
                else if (ext.hook && y == height_above)
                    out.rows.push_back(ext.hook);
                else
                    out.rows.push_back(ext.mid);
            }
            return out;
        }

        LayoutBox vstack_boxes(const std::vector<LayoutBox> &lines, bool centre)
        {
            int width = 0;
            int total = 0;
            for (const auto &line : lines)
            {
                width = std::max(width, line.width());
                total += static_cast<int>(line.rows.size());
            }
            LayoutBox out;
            if (total == 0)
                return math_text("");
            out.height_above = total / 2;
            out.height_below = total - 1 - out.height_above;
            out.rows.reserve(static_cast<size_t>(total));
            for (const auto &line : lines)
            {
                for (const auto &row : line.rows)
                {
                    if (!centre)
                    {
                        out.rows.push_back(row);
                        continue;
                    }
                    const int pad = width - ftxui::string_width(row);
                    const int left = pad / 2;
                    out.rows.push_back(spaces(left) + row + spaces(pad - left));
                }
            }
            return out;
        }

        std::string script_glyphs(std::string_view text, bool superscript, bool atomic = false)
        {
            std::span<const SymbolEntry> table =
                superscript ? std::span<const SymbolEntry>(kSuperScripts)
                            : std::span<const SymbolEntry>(kSubScripts);
            if (const auto *entry = lookup_symbol(table, text))
                return std::string(entry->glyph);
            if (atomic)
                return (superscript ? "^" : "_") + std::string(text);

            std::string result;
            size_t pos = 0;
            while (pos < text.size())
            {
                auto glyph = next_glyph(text, pos);
                const auto *entry = lookup_symbol(table, glyph);
                if (!entry)
                    return (superscript ? "^(" : "_(") + std::string(text) + ")";
                result += entry->glyph;
            }
            return result;
        }

        // ===================================================================
        // 3. TEX GRAMMAR & RECURSIVE DESCENT ENGINE
        // ===================================================================

        std::string read_delim_token(std::string_view input, size_t &pos)
        {
            while (pos < input.size() &&
                   std::isspace(static_cast<unsigned char>(input[pos])))
                ++pos;
            if (pos >= input.size())
                return ".";
            const char c = input[pos];
            if (c != '\\')
            {
                ++pos;
                return std::string(1, c);
            }
            ++pos;
            if (pos < input.size() &&
                !std::isalpha(static_cast<unsigned char>(input[pos])))
            {
                const char punct = input[pos++];
                return punct == '|' ? std::string("‖") : std::string(1, punct);
            }
            const size_t start = pos;
            while (pos < input.size() &&
                   std::isalpha(static_cast<unsigned char>(input[pos])))
                ++pos;
            const std::string_view name = input.substr(start, pos - start);
            if (const auto *entry = lookup_symbol(kNamedDelimiters, name))
                return std::string(entry->glyph);
            return ".";
        }

        std::optional<std::string_view> read_brace_group_raw(std::string_view input, size_t &pos)
        {
            if (pos >= input.size() || input[pos] != '{')
                return std::nullopt;
            size_t depth = 0;
            const size_t start = pos + 1;
            for (size_t i = pos; i < input.size(); ++i)
            {
                if (input[i] == '\\')
                {
                    ++i;
                    continue;
                }
                if (input[i] == '{')
                    ++depth;
                else if (input[i] == '}')
                {
                    if (--depth == 0)
                    {
                        std::string_view body = input.substr(start, i - start);
                        pos = i + 1;
                        return body;
                    }
                }
            }
            // Graceful recovery for streaming EOF: return partial group
            std::string_view body = input.substr(start);
            pos = input.size();
            return body;
        }

        std::vector<std::string_view> split_top_level(std::string_view text, char delim)
        {
            std::vector<std::string_view> parts;
            int brace_depth = 0;
            int env_depth = 0;
            size_t start = 0;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const char ch = text[i];
                if (ch == '\\')
                {
                    if (i + 1 < text.size() && text[i + 1] == '\\')
                    {
                        if (delim == '\\' && brace_depth == 0 && env_depth == 0)
                        {
                            parts.push_back(text.substr(start, i - start));
                            start = i + 2;
                        }
                        ++i;
                        continue;
                    }
                    size_t j = i + 1;
                    while (j < text.size() &&
                           std::isalpha(static_cast<unsigned char>(text[j])))
                        ++j;
                    const std::string_view macro = text.substr(i + 1, j - i - 1);
                    if (macro == "begin")
                        ++env_depth;
                    else if (macro == "end")
                        env_depth = std::max(0, env_depth - 1);
                    i = (j > i + 1) ? j - 1 : i + 1;
                    continue;
                }
                if (ch == '{')
                    ++brace_depth;
                else if (ch == '}')
                    brace_depth = std::max(0, brace_depth - 1);
                else if (ch == '&' && delim == '&' && brace_depth == 0 && env_depth == 0)
                {
                    parts.push_back(text.substr(start, i - start));
                    start = i + 1;
                }
            }
            parts.push_back(text.substr(start));
            return parts;
        }

        size_t find_env_end(std::string_view input, size_t pos,
                            std::string_view env_name, size_t &after_end)
        {
            const std::string begin_tag = "\\begin{" + std::string(env_name) + "}";
            const std::string end_tag = "\\end{" + std::string(env_name) + "}";
            int depth = 0;
            size_t i = pos;
            while (i < input.size())
            {
                const size_t b = input.find(begin_tag, i);
                const size_t e = input.find(end_tag, i);
                if (e == std::string_view::npos)
                    return std::string_view::npos;
                if (b != std::string_view::npos && b < e)
                {
                    ++depth;
                    i = b + begin_tag.size();
                    continue;
                }
                if (depth == 0)
                {
                    after_end = e + end_tag.size();
                    return e;
                }
                --depth;
                i = e + end_tag.size();
            }
            return std::string_view::npos;
        }

        std::optional<LayoutBox> parse_math_group(std::string_view input, size_t &pos,
                                                  MathMode mode, bool text_mode = false);

        LayoutBox parse_math_sequence(std::string_view input, size_t &pos, char stop,
                                      MathMode mode, bool text_mode = false,
                                      std::string *right_delim_out = nullptr,
                                      bool allow_linebreaks = false);

        std::optional<LayoutBox> parse_math_matrix(std::string_view input, size_t &pos,
                                                   std::string_view env_name,
                                                   const EnvTraits &traits,
                                                   std::string_view colspec,
                                                   MathMode mode)
        {
            size_t after_end = 0;
            const size_t end_pos = find_env_end(input, pos, env_name, after_end);
            if (end_pos == std::string_view::npos)
                return std::nullopt;

            std::string_view body = input.substr(pos, end_pos - pos);
            pos = after_end;

            std::string align_spec;
            for (char sc : colspec)
                if (sc == 'l' || sc == 'c' || sc == 'r')
                    align_spec += sc;

            const bool pad_odd_cells = traits.columns == ColumnStyle::AlignPairs;

            std::vector<std::vector<std::string>> raw_grid;
            for (std::string_view raw_row : split_top_level(body, '\\'))
            {
                std::vector<std::string> raw_cells;
                size_t cell_index = 0;
                for (std::string_view raw_cell : split_top_level(raw_row, '&'))
                {
                    if (pad_odd_cells && (cell_index % 2) == 1)
                        raw_cells.push_back("{}" + std::string(raw_cell));
                    else
                        raw_cells.push_back(std::string(raw_cell));
                    ++cell_index;
                }
                const bool empty_row =
                    raw_cells.size() == 1 &&
                    raw_cells[0].find_first_not_of(" \t\r\n") == std::string::npos;
                if (!empty_row)
                    raw_grid.push_back(std::move(raw_cells));
            }

            if (raw_grid.empty())
                return math_text("");

            if (mode == MathMode::Inline)
            {
                std::string line;
                if (traits.left_delim != ".")
                    line += traits.left_delim;
                for (size_t r = 0; r < raw_grid.size(); ++r)
                {
                    if (r > 0)
                        line += "; ";
                    for (size_t c = 0; c < raw_grid[r].size(); ++c)
                    {
                        size_t cpos = 0;
                        auto box = parse_math_sequence(raw_grid[r][c], cpos, '\0', mode);
                        const std::string cell = trim_spaces(flatten_box(box));
                        if (cell.empty())
                            continue;
                        if (!line.empty() && line.back() != ' ' &&
                            line != traits.left_delim)
                            line += " ";
                        line += cell;
                    }
                }
                if (traits.right_delim != ".")
                    line += traits.right_delim;
                return math_text(line);
            }

            size_t nrows = raw_grid.size();
            size_t ncols = 0;
            for (const auto &row : raw_grid)
                ncols = std::max(ncols, row.size());
            if (ncols == 0)
                return math_text("");

            std::vector<std::vector<LayoutBox>> grid(nrows);
            std::vector<int> col_widths(ncols, 0);
            std::vector<int> row_heights_above(nrows, 0);
            std::vector<int> row_heights_below(nrows, 0);

            for (size_t r = 0; r < nrows; ++r)
            {
                grid[r].reserve(ncols);
                for (size_t c = 0; c < ncols; ++c)
                {
                    if (c < raw_grid[r].size())
                    {
                        size_t cpos = 0;
                        auto box = parse_math_sequence(raw_grid[r][c], cpos, '\0', mode);
                        col_widths[c] = std::max(col_widths[c], box.width());
                        row_heights_above[r] = std::max(row_heights_above[r], box.height_above);
                        row_heights_below[r] = std::max(row_heights_below[r], box.height_below);
                        grid[r].push_back(std::move(box));
                    }
                    else
                    {
                        grid[r].push_back(math_text(""));
                    }
                }
            }

            std::vector<LayoutBox> row_boxes;
            row_boxes.reserve(nrows);
            for (size_t r = 0; r < nrows; ++r)
            {
                std::vector<LayoutBox> aligned_cells;
                aligned_cells.reserve(ncols * 2);
                for (size_t c = 0; c < ncols; ++c)
                {
                    if (c > 0)
                    {
                        if (traits.columns == ColumnStyle::AlignPairs)
                            aligned_cells.push_back(
                                math_text((c % 2) == 1 ? "" : "    "));
                        else
                            aligned_cells.push_back(
                                math_text(traits.wide_gap ? "   " : "  "));
                    }
                    LayoutBox cell = std::move(grid[r][c]);
                    const int pad = col_widths[c] - cell.width();
                    int left_pad = pad / 2;
                    switch (traits.columns)
                    {
                    case ColumnStyle::Centred:
                        break;
                    case ColumnStyle::LeftAll:
                        left_pad = 0;
                        break;
                    case ColumnStyle::AlignPairs:
                        left_pad = (c % 2) == 0 ? pad : 0;
                        break;
                    case ColumnStyle::Spec:
                    {
                        const char al = c < align_spec.size() ? align_spec[c] : 'c';
                        left_pad = al == 'l' ? 0 : al == 'r' ? pad : pad / 2;
                        break;
                    }
                    }
                    const int right_pad = pad - left_pad;
                    if (left_pad > 0 || right_pad > 0)
                    {
                        const std::string lpad = spaces(left_pad);
                        const std::string rpad = spaces(right_pad);
                        for (auto &row : cell.rows)
                            row = lpad + row + rpad;
                    }
                    const int add_above = row_heights_above[r] - cell.height_above;
                    const int add_below = row_heights_below[r] - cell.height_below;
                    if (add_above > 0)
                        cell.rows.insert(cell.rows.begin(),
                                         static_cast<size_t>(add_above), std::string());
                    if (add_below > 0)
                        cell.rows.insert(cell.rows.end(),
                                         static_cast<size_t>(add_below), std::string());
                    cell.height_above = row_heights_above[r];
                    cell.height_below = row_heights_below[r];
                    aligned_cells.push_back(std::move(cell));
                }
                row_boxes.push_back(math_hrow(aligned_cells));
            }

            int total_height = 0;
            for (const auto &rb : row_boxes)
                total_height += rb.total_height();

            LayoutBox matrix_body;
            matrix_body.height_above = total_height / 2;
            matrix_body.height_below = total_height - 1 - matrix_body.height_above;
            matrix_body.rows.reserve(static_cast<size_t>(total_height));

            for (size_t r = 0; r < nrows; ++r)
            {
                for (const auto &row_str : row_boxes[r].rows)
                {
                    matrix_body.rows.push_back(row_str);
                }
            }

            if (traits.left_delim == "." && traits.right_delim == ".")
                return matrix_body;

            LayoutBox left_box = make_delimiter_column(
                traits.left_delim, matrix_body.height_above, matrix_body.height_below);
            LayoutBox right_box = make_delimiter_column(
                traits.right_delim, matrix_body.height_above, matrix_body.height_below);
            const LayoutBox parts[3] = {std::move(left_box), std::move(matrix_body),
                                        std::move(right_box)};
            return math_hrow(parts);
        }

        LayoutBox parse_math_sequence(std::string_view input, size_t &pos, char stop,
                                      MathMode mode, bool text_mode,
                                      std::string *right_delim_out,
                                      bool allow_linebreaks)
        {
            std::vector<LayoutBox> pieces;
            std::vector<LayoutBox> completed_lines;
            bool prev_was_operator = false;
            bool prev_is_bigop = false;

            auto append = [&](LayoutBox value)
            {
                pieces.push_back(std::move(value));
                prev_was_operator = false;
                prev_is_bigop = false;
            };

            auto append_symbol = [&](const std::string_view &glyph)
            {
                const bool spaced = !text_mode && is_spaced_operator(glyph) &&
                                    !pieces.empty() && !prev_was_operator;
                append(math_text(spaced ? " " + std::string(glyph) + " "
                                        : std::string(glyph)));
                prev_was_operator = !text_mode && is_spaced_operator(glyph);
                prev_is_bigop = is_big_operator(glyph);
            };

            while (pos < input.size() && input[pos] != stop)
            {
                const char c = input[pos];
                if (c == '{')
                {
                    auto group = parse_math_group(input, pos, mode);
                    if (group)
                        append(std::move(*group));
                    else
                        append(math_text("{"));
                    continue;
                }
                if (c == '^' || c == '_')
                {
                    const bool superscript = c == '^';
                    ++pos;
                    LayoutBox script = math_text("");
                    std::string raw_script_glyph;
                    bool script_is_atomic = false;
                    if (pos < input.size() && input[pos] == '{')
                    {
                        auto group = parse_math_group(input, pos, mode);
                        if (group)
                            script = std::move(*group);
                    }
                    else if (pos < input.size() && input[pos] == '\\')
                    {
                        size_t lookahead = pos + 1;
                        while (lookahead < input.size() &&
                               std::isalpha(static_cast<unsigned char>(input[lookahead])))
                            ++lookahead;
                        std::string_view macro_name =
                            input.substr(pos + 1, lookahead - (pos + 1));
                        pos = lookahead;
                        if (const auto *entry = lookup_symbol(kMathSymbols, macro_name))
                            raw_script_glyph = std::string(entry->glyph);
                        else
                            raw_script_glyph = std::string(macro_name);
                        script_is_atomic = true;
                        script = math_text(raw_script_glyph);
                    }
                    else if (pos < input.size())
                    {
                        raw_script_glyph = next_glyph(input, pos);
                        script = math_text(raw_script_glyph);
                    }

                    // BUG FIX: Leading script without preceding atom (e.g. ^{238}U, _{0}^{1}).
                    // Initialize empty base atom so the script is not silently dropped.
                    if (pieces.empty())
                    {
                        pieces.push_back(math_text(""));
                    }

                    std::string text_to_map = !raw_script_glyph.empty()
                                                  ? raw_script_glyph
                                                  : (script.rows.size() == 1
                                                         ? script.rows.front()
                                                         : "");
                    std::string mapped =
                        script_glyphs(text_to_map, superscript, script_is_atomic);

                    const bool mapped_cleanly = !mapped.empty() &&
                                                mapped.find('^') == std::string::npos &&
                                                mapped.find('_') == std::string::npos;

                    const bool stack_on_bigop =
                        prev_is_bigop && mode == MathMode::Display;

                    if (stack_on_bigop)
                    {
                        pieces.back() =
                            stack_limit(pieces.back(), script, superscript);
                        prev_is_bigop = true;
                        continue;
                    }

                    const bool base_is_e = superscript &&
                                           mode == MathMode::Inline &&
                                           trim_spaces(flatten_box(pieces.back())) == "e";

                    if (base_is_e && !mapped_cleanly)
                    {
                        pieces.back() = math_text("exp(" + text_to_map + ")");
                    }
                    else if (script.rows.size() > 1)
                    {
                        pieces.back() = stack_limit(pieces.back(), script, superscript);
                    }
                    else if (mapped_cleanly || mode == MathMode::Inline)
                    {
                        pieces.back() = math_hbox(pieces.back(), math_text(mapped));
                    }
                    else
                    {
                        std::string clean_text = text_to_map;
                        if (!mapped.empty() && (mapped[0] == '^' || mapped[0] == '_'))
                            clean_text = mapped.substr(1);
                        LayoutBox elevated = math_text(clean_text);
                        if (superscript)
                        {
                            elevated.height_above = 1;
                            elevated.rows.push_back(spaces(elevated.width()));
                        }
                        else
                        {
                            elevated.height_below = 1;
                            elevated.rows.insert(elevated.rows.begin(),
                                                 spaces(elevated.width()));
                        }
                        pieces.back() = math_hbox(pieces.back(), elevated);
                    }
                    continue;
                }
                if (c == '\\')
                {
                    ++pos;
                    const size_t name_start = pos;
                    while (pos < input.size() &&
                           std::isalpha(static_cast<unsigned char>(input[pos])))
                        ++pos;
                    const std::string_view name =
                        input.substr(name_start, pos - name_start);
                    if (name.empty() && pos < input.size())
                    {
                        const char spacing = input[pos++];
                        if (spacing == ',' || spacing == ';')
                            append(math_text(" "));
                        else if (spacing == '\\')
                        {
                            if (allow_linebreaks && !text_mode)
                            {
                                if (pos < input.size() && input[pos] == '[')
                                {
                                    const size_t close = input.find(']', pos);
                                    if (close != std::string_view::npos)
                                        pos = close + 1;
                                }
                                completed_lines.push_back(
                                    pieces.empty() ? math_text("") : math_hrow(pieces));
                                pieces.clear();
                                prev_was_operator = false;
                                prev_is_bigop = false;
                            }
                            else
                                append(math_text("  "));
                        }
                        else if (spacing != '!')
                            append(math_text(std::string("\\") + spacing));
                    }
                    else if (name == "frac")
                    {
                        auto numerator = parse_math_group(input, pos, mode);
                        auto denominator = parse_math_group(input, pos, mode);
                        if (numerator && denominator)
                            append(math_fraction(*numerator, *denominator, mode));
                        else
                            append(math_text("\\frac"));
                    }
                    else if (name == "sqrt")
                    {
                        std::optional<LayoutBox> root_index;
                        if (pos < input.size() && input[pos] == '[')
                        {
                            ++pos;
                            root_index = parse_math_sequence(input, pos, ']', mode);
                            if (pos < input.size() && input[pos] == ']')
                                ++pos;
                        }
                        auto body = parse_math_group(input, pos, mode);
                        if (body && mode == MathMode::Inline)
                        {
                            const std::string radicand = trim_spaces(flatten_box(*body));
                            const std::string index =
                                root_index ? trim_spaces(flatten_box(*root_index))
                                           : std::string();
                            std::string prefix = "√";
                            if (!index.empty())
                            {
                                std::string super_idx = script_glyphs(index, true);
                                if (!super_idx.empty() && super_idx.find('^') == std::string::npos)
                                    prefix = super_idx + "√";
                                else
                                    prefix = index + "√";
                            }
                            append(math_text(prefix + (needs_parens(radicand)
                                                           ? "(" + radicand + ")"
                                                           : radicand)));
                        }
                        else if (body)
                        {
                            // BUG FIX: Display mode \sqrt[n]{...} preserve root index
                            LayoutBox body_padded = std::move(*body);
                            const int radicand_width = body_padded.width();
                            body_padded.rows.insert(body_padded.rows.begin(),
                                                    rule(radicand_width));
                            body_padded.height_above += 1;

                            const std::string index =
                                root_index ? trim_spaces(flatten_box(*root_index))
                                           : std::string();
                            std::string prefix_sym = "√";
                            if (!index.empty())
                            {
                                std::string super_idx = script_glyphs(index, true);
                                if (!super_idx.empty() && super_idx.find('^') == std::string::npos)
                                    prefix_sym = super_idx + "√";
                                else
                                    prefix_sym = index + "√";
                            }
                            LayoutBox prefix;
                            prefix.height_above = body_padded.height_above;
                            prefix.height_below = body_padded.height_below;
                            prefix.rows.assign(
                                static_cast<size_t>(body_padded.total_height()), "");
                            prefix.rows[static_cast<size_t>(prefix.height_above)] =
                                prefix_sym;

                            append(math_hbox(prefix, body_padded));
                        }
                        else
                            append(math_text("\\sqrt"));
                    }
                    else if (name == "text" || name == "mathrm" || name == "mathbf" ||
                             name == "mathit" || name == "mathbr" ||
                             name == "mathsf" || name == "mathtt" || name == "mathnormal" ||
                             name == "bm" || name == "symbf" || name == "operatorname" ||
                             name == "boldsymbol" ||
                             name == "underbrace" || name == "overbrace" ||
                             name == "underline" || name == "overline")
                    {
                        if (name == "operatorname" && pos < input.size() && input[pos] == '*')
                            ++pos;
                        auto body = parse_math_group(input, pos, mode, /*text_mode=*/true);
                        if (body)
                            append(std::move(*body));
                        else
                            append(math_text("\\" + std::string(name)));
                    }
                    else if (const auto *accent = lookup_symbol(kAccents, name))
                    {
                        auto body = parse_math_group(input, pos, mode);
                        LayoutBox target;
                        if (body)
                            target = std::move(*body);
                        else if (pos < input.size())
                            target = math_text(next_glyph(input, pos));
                        if (!target.rows.empty())
                        {
                            target.rows.front() += accent->glyph;
                            append(std::move(target));
                        }
                        else
                        {
                            append(math_text("\\" + std::string(name)));
                        }
                    }
                    else if (name == "int" || name == "oint" || name == "iint" || name == "iiint")
                    {
                        append(math_integral(1, name));
                        prev_is_bigop = true;
                    }
                    else if (name == "binom" || name == "dbinom" || name == "tbinom")
                    {
                        auto top = parse_math_group(input, pos, mode);
                        auto bottom = parse_math_group(input, pos, mode);
                        if (top && bottom)
                        {
                            if (mode == MathMode::Inline)
                            {
                                append(math_text("C(" + trim_spaces(flatten_box(*top)) +
                                                 ", " + trim_spaces(flatten_box(*bottom)) + ")"));
                            }
                            else
                            {
                                LayoutBox stacked =
                                    vstack_boxes({*top, *bottom}, /*centre=*/true);
                                LayoutBox left_col = make_delimiter_column(
                                    "(", stacked.height_above, stacked.height_below);
                                LayoutBox right_col = make_delimiter_column(
                                    ")", stacked.height_above, stacked.height_below);
                                const LayoutBox binom_parts[3] = {
                                    std::move(left_col), std::move(stacked),
                                    std::move(right_col)};
                                append(math_hrow(binom_parts));
                            }
                        }
                        else
                            append(math_text("\\binom"));
                    }
                    else if (name == "overset" || name == "underset" || name == "stackrel")
                    {
                        const bool above = name != "underset";
                        auto anno = parse_math_group(input, pos, mode);
                        auto base = parse_math_group(input, pos, mode);
                        if (anno && base)
                        {
                            if (mode == MathMode::Display)
                                append(stack_limit(*base, *anno, above));
                            else
                            {
                                const std::string a = trim_spaces(flatten_box(*anno));
                                const std::string b = trim_spaces(flatten_box(*base));
                                const std::string mapped = script_glyphs(a, above);
                                const bool clean = !mapped.empty() &&
                                                   mapped.find('^') == std::string::npos &&
                                                   mapped.find('_') == std::string::npos;
                                append(math_text(clean ? b + mapped
                                                       : b + "(" + a + ")"));
                            }
                        }
                        else
                            append(math_text("\\" + std::string(name)));
                    }
                    else if (name == "substack")
                    {
                        auto raw = read_brace_group_raw(input, pos);
                        if (raw)
                        {
                            std::vector<LayoutBox> rows;
                            std::string joined;
                            size_t rstart = 0;
                            while (rstart <= raw->size())
                            {
                                size_t rend = raw->find("\\\\", rstart);
                                if (rend == std::string_view::npos)
                                    rend = raw->size();
                                size_t rpos = 0;
                                const std::string_view row_src =
                                    raw->substr(rstart, rend - rstart);
                                LayoutBox row =
                                    parse_math_sequence(row_src, rpos, '\0', MathMode::Inline);
                                const std::string flat = trim_spaces(flatten_box(row));
                                if (!flat.empty())
                                {
                                    if (!joined.empty())
                                        joined += ", ";
                                    joined += flat;
                                    rows.push_back(math_text(flat));
                                }
                                if (rend == raw->size())
                                    break;
                                rstart = rend + 2;
                            }
                            if (mode == MathMode::Display && rows.size() > 1)
                                append(vstack_boxes(rows, /*centre=*/true));
                            else
                                append(math_text(joined));
                        }
                        else
                            append(math_text("\\substack"));
                    }
                    else if (name == "xrightarrow" || name == "xleftarrow")
                    {
                        std::optional<LayoutBox> below;
                        if (pos < input.size() && input[pos] == '[')
                        {
                            ++pos;
                            below = parse_math_sequence(input, pos, ']', mode);
                            if (pos < input.size() && input[pos] == ']')
                                ++pos;
                        }
                        auto above = parse_math_group(input, pos, mode);
                        const bool rightward = name == "xrightarrow";
                        const std::string label =
                            above ? trim_spaces(flatten_box(*above)) : std::string();
                        const std::string below_label =
                            below ? trim_spaces(flatten_box(*below)) : std::string();
                        if (mode == MathMode::Inline ||
                            (label.empty() && below_label.empty()))
                        {
                            std::string combined = label;
                            if (!below_label.empty())
                                combined += (combined.empty() ? "" : "/") + below_label;
                            std::string arrow;
                            if (combined.empty())
                                arrow = rightward ? "→" : "←";
                            else if (rightward)
                                arrow = "─" + combined + "→";
                            else
                                arrow = "←" + combined + "─";
                            append(math_text(arrow));
                        }
                        else
                        {
                            const int label_width = std::max(
                                ftxui::string_width(label),
                                ftxui::string_width(below_label));
                            LayoutBox shaft;
                            if (rightward)
                                shaft = math_text(rule(label_width + 1) + "→");
                            else
                                shaft = math_text("←" + rule(label_width + 1));
                            if (!label.empty())
                                shaft = stack_limit(shaft, math_text(label), /*above=*/true);
                            if (!below_label.empty())
                                shaft = stack_limit(shaft, math_text(below_label), /*above=*/false);
                            append(std::move(shaft));
                        }
                    }
                    else if (name == "phantom" || name == "hphantom" || name == "vphantom")
                    {
                        auto body = parse_math_group(input, pos, mode);
                        if (body)
                        {
                            if (name == "vphantom")
                            {
                                LayoutBox ghost;
                                ghost.height_above = body->height_above;
                                ghost.height_below = body->height_below;
                                ghost.rows.assign(
                                    static_cast<size_t>(body->total_height()), "");
                                append(std::move(ghost));
                            }
                            else
                                append(math_text(spaces(body->width())));
                        }
                    }
                    else if (name == "left")
                    {
                        const std::string open_delim = read_delim_token(input, pos);
                        std::string close_delim;
                        LayoutBox body = parse_math_sequence(input, pos, stop, mode,
                                                             text_mode, &close_delim);
                        if (close_delim.empty())
                            close_delim = ".";
                        if (mode == MathMode::Inline || body.total_height() <= 1)
                        {
                            std::vector<LayoutBox> parts;
                            if (open_delim != ".")
                                parts.push_back(math_text(open_delim));
                            parts.push_back(std::move(body));
                            if (close_delim != ".")
                                parts.push_back(math_text(close_delim));
                            append(math_hrow(parts));
                        }
                        else
                        {
                            LayoutBox left_col = make_delimiter_column(
                                open_delim, body.height_above, body.height_below);
                            LayoutBox right_col = make_delimiter_column(
                                close_delim, body.height_above, body.height_below);
                            const LayoutBox delim_parts[3] = {
                                std::move(left_col), std::move(body),
                                std::move(right_col)};
                            append(math_hrow(delim_parts));
                        }
                    }
                    else if (name == "right")
                    {
                        const std::string delim = read_delim_token(input, pos);
                        if (right_delim_out)
                        {
                            *right_delim_out = delim;
                            break;
                        }
                        if (delim != ".")
                            append(math_text(delim));
                    }
                    else if (name == "middle")
                    {
                        const std::string delim = read_delim_token(input, pos);
                        if (delim != ".")
                            append(math_text(delim));
                    }
                    else if (name == "begin")
                    {
                        auto env = parse_math_group(input, pos, mode);
                        if (env)
                        {
                            const std::string env_name = trim_spaces(flatten_box(*env));
                            std::string_view base_name = env_name;
                            if (!base_name.empty() && base_name.back() == '*')
                                base_name.remove_suffix(1);
                            if (const auto *traits = lookup_env_traits(base_name))
                            {
                                std::string colspec;
                                if (base_name == "array")
                                    if (auto raw = read_brace_group_raw(input, pos))
                                        colspec = std::string(*raw);
                                auto matrix_box = parse_math_matrix(
                                    input, pos, env_name, *traits, colspec, mode);
                                if (matrix_box)
                                {
                                    append(std::move(*matrix_box));
                                    continue;
                                }
                            }
                            else
                            {
                                size_t after_end = 0;
                                const size_t end_pos =
                                    find_env_end(input, pos, env_name, after_end);
                                if (end_pos != std::string_view::npos)
                                {
                                    std::string_view env_body =
                                        input.substr(pos, end_pos - pos);
                                    size_t bpos = 0;
                                    append(parse_math_sequence(
                                        env_body, bpos, '\0', mode, text_mode, nullptr,
                                        /*allow_linebreaks=*/mode == MathMode::Display));
                                    pos = after_end;
                                    continue;
                                }
                            }
                        }
                    }
                    else if (name == "end")
                    {
                        auto env = parse_math_group(input, pos, mode);
                        (void)env;
                    }
                    else if (name == "pmatrix" || name == "vmatrix" || name == "bmatrix" ||
                             name == "matrix" || name == "aligned")
                    {
                        // Standalone matrix tags consumed silently
                    }
                    else if (name == "large" || name == "Large" || name == "LARGE" ||
                             name == "huge" || name == "Huge" || name == "small" ||
                             name == "tiny" || name == "normalsize" ||
                             name == "displaystyle" || name == "textstyle" ||
                             name == "scriptstyle" || name == "scriptscriptstyle" ||
                             name == "notag" || name == "nonumber")
                    {
                        // Sizing/style/numbering directives
                    }
                    else if (name == "limits")
                    {
                        prev_is_bigop = true;
                    }
                    else if (name == "nolimits")
                    {
                        prev_is_bigop = false;
                    }
                    else if (name == "color" || name == "textcolor" ||
                             name == "label" || name == "tag")
                    {
                        if (name == "tag" && pos < input.size() && input[pos] == '*')
                            ++pos;
                        auto dropped = parse_math_group(input, pos, mode, /*text_mode=*/true);
                        (void)dropped;
                    }
                    else if (name == "mathbb" || name == "mathfrak" ||
                             name == "mathscr" || name == "mathcal")
                    {
                        auto body = parse_math_group(input, pos, mode);
                        if (body)
                        {
                            const MathAlphabet which =
                                name == "mathbb"     ? MathAlphabet::DoubleStruck
                                : name == "mathfrak" ? MathAlphabet::Fraktur
                                                     : MathAlphabet::Script;
                            for (auto &row : body->rows)
                                row = map_math_alphabet(row, which);
                            append(std::move(*body));
                        }
                        else
                            append(math_text("\\" + std::string(name)));
                    }
                    else if (name == "quad" || name == "qquad")
                    {
                        append(math_text(name == "quad" ? "  " : "    "));
                    }
                    else if (name == "bmod")
                    {
                        append(math_text(" mod "));
                    }
                    else if (name == "pmod")
                    {
                        auto body = parse_math_group(input, pos, mode);
                        append(math_text(body ? " (mod " + trim_spaces(flatten_box(*body)) + ")"
                                              : " (mod)"));
                    }
                    else
                    {
                        if (const auto *entry = lookup_symbol(kMathSymbols, name))
                            append_symbol(entry->glyph);
                        else if (name.size() > 1)
                            append(math_text(std::string(name)));
                        else
                            append(math_text("\\" + std::string(name)));
                    }
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(c)))
                {
                    while (pos < input.size() &&
                           std::isspace(static_cast<unsigned char>(input[pos])))
                        ++pos;
                    if (text_mode)
                        append(math_text(" "));
                    continue;
                }
                {
                    std::string glyph = next_glyph(input, pos);
                    const bool spaced = !text_mode && is_spaced_operator(glyph) &&
                                        !pieces.empty() && !prev_was_operator;
                    if (spaced)
                    {
                        append(math_text(" " + glyph + " "));
                    }
                    else if (!text_mode && (glyph == "," || glyph == ";"))
                    {
                        append(math_text(glyph + " "));
                    }
                    else
                    {
                        append(math_text(glyph));
                    }
                    prev_was_operator = !text_mode && is_spaced_operator(glyph);
                    continue;
                }
            }

            if (!completed_lines.empty())
            {
                if (!pieces.empty())
                    completed_lines.push_back(math_hrow(pieces));
                return vstack_boxes(completed_lines, /*centre=*/false);
            }
            if (pieces.empty())
                return math_text("");
            return math_hrow(pieces);
        }

        std::optional<LayoutBox> parse_math_group(std::string_view input, size_t &pos,
                                                  MathMode mode, bool text_mode)
        {
            if (pos >= input.size() || input[pos] != '{')
                return std::nullopt;
            ++pos;
            auto result = parse_math_sequence(input, pos, '}', mode, text_mode);
            if (pos < input.size() && input[pos] == '}')
                ++pos;
            return result;
        }

        std::string math_to_text(std::string_view source)
        {
            size_t pos = 0;
            LayoutBox layout = parse_math_sequence(source, pos, '\0', MathMode::Inline);
            return layout.rows.empty() ? std::string() : flatten_box(layout);
        }

        // ===================================================================
        // 4. DELIMITER PRE-PROCESSING & SPAN NORMALIZATION
        // ===================================================================

        bool looks_like_math(std::string_view span)
        {
            for (size_t i = 0; i < span.size(); ++i)
            {
                if (span[i] == '^' || span[i] == '_')
                    return true;
                if (span[i] == '\\' && i + 1 < span.size() &&
                    std::isalpha(static_cast<unsigned char>(span[i + 1])))
                    return true;
            }
            return false;
        }

        size_t backtick_run(std::string_view text, size_t pos)
        {
            size_t n = 0;
            while (pos + n < text.size() && text[pos + n] == '`')
                ++n;
            return n;
        }

        std::string escape_math_content(std::string_view span)
        {
            std::string out;
            out.reserve(span.size() * 2);
            for (char c : span)
            {
                if (std::ispunct(static_cast<unsigned char>(c)))
                    out += '\\';
                out += c;
            }
            return out;
        }

        bool escaped_at(std::string_view text, size_t pos)
        {
            size_t slashes = 0;
            while (pos > 0 && text[pos - 1] == '\\')
            {
                ++slashes;
                --pos;
            }
            return (slashes % 2) != 0;
        }

    } // namespace

    // =======================================================================
    // 5. PUBLIC INTERFACE & FACADE
    // =======================================================================

    std::string next_glyph(std::string_view input, size_t &pos)
    {
        if (pos >= input.size())
            return {};
        const size_t start = pos++;
        if (static_cast<unsigned char>(input[start]) < 0x80)
            return std::string(input.substr(start, 1));
        while (pos < input.size() &&
               (static_cast<unsigned char>(input[pos]) & 0xC0) == 0x80)
            ++pos;
        return std::string(input.substr(start, pos - start));
    }

    std::optional<std::pair<size_t, size_t>> find_math_span(std::string_view text,
                                                            size_t from)
    {
        size_t scan_pos = from;
        while (scan_pos < text.size())
        {
            size_t open = text.find('$', scan_pos);
            while (open != std::string_view::npos && escaped_at(text, open))
                open = text.find('$', open + 1);
            if (open == std::string_view::npos)
                return std::nullopt;

            const size_t delimiter_size =
                (open + 1 < text.size() && text[open + 1] == '$') ? 2 : 1;

            if (delimiter_size == 2)
            {
                size_t close = text.find("$$", open + 2);
                while (close != std::string_view::npos && escaped_at(text, close))
                    close = text.find("$$", close + 1);
                if (close == std::string_view::npos)
                {
                    // BUG FIX: Resume scanning past open $$ instead of aborting
                    scan_pos = open + 2;
                    continue;
                }
                return std::make_pair(open, close + 2);
            }
            else
            {
                // Single $ inline math cannot cross newlines
                size_t next_newline = text.find('\n', open + 1);
                size_t close = text.find('$', open + 1);
                bool found = false;
                while (close != std::string_view::npos &&
                       (next_newline == std::string_view::npos || close < next_newline))
                {
                    // BUG FIX: Ensure closing single $ is not escaped, and neither
                    // followed by $ nor preceded by $ (avoiding 2nd dollar of $$).
                    const bool not_escaped = !escaped_at(text, close);
                    const bool not_followed_by_dollar =
                        (close + 1 >= text.size() || text[close + 1] != '$');
                    const bool not_preceded_by_dollar =
                        (close == 0 || text[close - 1] != '$');
                    if (not_escaped && not_followed_by_dollar && not_preceded_by_dollar)
                    {
                        return std::make_pair(open, close + 1);
                    }
                    close = text.find('$', close + 1);
                }
                // BUG FIX: Unmatched opening $ on this line; advance past open and continue scanning
                scan_pos = open + 1;
            }
        }
        return std::nullopt;
    }

    bool contains_math(std::string_view text)
    {
        return find_math_span(text, 0).has_value();
    }

    std::string NormalizeMathDelimiters(std::string_view source)
    {
        std::string out;
        out.reserve(source.size());

        bool at_line_start = true;
        bool in_fence = false;
        size_t i = 0;
        while (i < source.size())
        {
            const char c = source[i];

            if (at_line_start && (c == '`' || c == '~'))
            {
                const size_t run = backtick_run(source, i);
                const size_t tilde = (c == '~') ? [&]
                {
                    size_t n = 0;
                    while (i + n < source.size() && source[i + n] == '~')
                        ++n;
                    return n;
                }()
                                                : 0;
                if (run >= 3 || tilde >= 3)
                {
                    in_fence = !in_fence;
                    const size_t width = run >= 3 ? run : tilde;
                    out.append(source.substr(i, width));
                    i += width;
                    at_line_start = false;
                    continue;
                }
            }
            if (in_fence)
            {
                out += c;
                at_line_start = (c == '\n');
                ++i;
                continue;
            }

            if (c == '`')
            {
                const size_t run = backtick_run(source, i);
                const size_t close = source.find(std::string(run, '`'), i + run);
                const size_t end = close == std::string_view::npos
                                       ? source.size()
                                       : close + run;
                out.append(source.substr(i, end - i));
                i = end;
                at_line_start = false;
                continue;
            }

            if (c == '\\' && source.compare(i, 7, "\\begin{") == 0)
            {
                const size_t name_start = i + 7;
                const size_t name_end = source.find('}', name_start);
                if (name_end != std::string_view::npos &&
                    is_known_math_environment(
                        source.substr(name_start, name_end - name_start)))
                {
                    const std::string_view env_name =
                        source.substr(name_start, name_end - name_start);
                    size_t after_end = 0;
                    if (find_env_end(source, name_end + 1, env_name, after_end) !=
                        std::string_view::npos)
                    {
                        out.append("$$");
                        out += escape_math_content(source.substr(i, after_end - i));
                        out.append("$$");
                        i = after_end;
                        at_line_start = false;
                        continue;
                    }
                }
            }

            if (c == '\\' && i + 1 < source.size() &&
                (source[i + 1] == '(' || source[i + 1] == '['))
            {
                const bool display = source[i + 1] == '[';
                const std::string_view closer = display ? "\\]" : "\\)";
                const size_t body = i + 2;
                const size_t close = source.find(closer, body);
                if (close != std::string_view::npos &&
                    looks_like_math(source.substr(body, close - body)))
                {
                    const std::string_view fence = display ? "$$" : "$";
                    out.append(fence);
                    out += escape_math_content(source.substr(body, close - body));
                    out.append(fence);
                    i = close + closer.size();
                    at_line_start = false;
                    continue;
                }
            }

            if (c == '$')
            {
                if (auto span = find_math_span(source, i); span && span->first == i)
                {
                    const size_t delim =
                        (i + 1 < source.size() && source[i + 1] == '$') ? 2 : 1;
                    const std::string_view fence = source.substr(i, delim);
                    const size_t body = i + delim;
                    const size_t body_end = span->second - delim;
                    out.append(fence);
                    out += escape_math_content(source.substr(body, body_end - body));
                    out.append(fence);
                    i = span->second;
                    at_line_start = false;
                    continue;
                }
            }

            out += c;
            at_line_start = (c == '\n');
            ++i;
        }
        return out;
    }

    std::string substitute_inline_math(std::string_view text)
    {
        std::string out;
        size_t cursor = 0;
        while (cursor < text.size())
        {
            auto span = find_math_span(text, cursor);
            if (!span)
            {
                out.append(text.substr(cursor));
                break;
            }
            const auto [open, end] = *span;
            out.append(text.substr(cursor, open - cursor));
            const size_t delim =
                (open + 1 < text.size() && text[open + 1] == '$') ? 2 : 1;
            out += math_to_text(text.substr(open + delim, (end - delim) - (open + delim)));
            cursor = end;
        }
        return out;
    }

    ftxui::Element render_math(std::string_view source, ftxui::Decorator style, MathMode mode)
    {
        size_t pos = 0;
        LayoutBox layout = parse_math_sequence(source, pos, '\0', mode,
                                               /*text_mode=*/false,
                                               /*right_delim_out=*/nullptr,
                                               /*allow_linebreaks=*/mode == MathMode::Display);
        if (layout.rows.empty())
            return ftxui::text("") | style;
        if (mode == MathMode::Inline)
            return ftxui::text(flatten_box(layout)) | style;
        ftxui::Elements lines;
        lines.reserve(layout.rows.size());
        for (auto &row : layout.rows)
            lines.push_back(ftxui::text(std::move(row)));
        if (lines.empty())
            lines.push_back(ftxui::text(""));
        return ftxui::vbox(std::move(lines)) | style;
    }

} // namespace ftxui::ext
