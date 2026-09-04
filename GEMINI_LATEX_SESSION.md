# GEMINI LaTeX Session: Refactoring & Architecture Report

## 1. Executive Summary

This session performed a comprehensive architectural refactoring, hardening, and modernization of the terminal-Unicode LaTeX math rendering engine (`src/latex_math.cc` and `include/ftxui/ext/latex_math.h`).

Key achievements:
- **Zero-Allocation Data-Driven Architecture**: Replaced dynamic `unordered_map` / `unordered_set` runtime static containers (`SvMap`, `SvSet`) with `constexpr` POD structures stored directly in `.rodata`, looked up via `std::lower_bound` / `std::binary_search` over `std::string_view`.
- **Clean Separation of Concerns**: Partitioned a 2,500+ line monolithic file into 5 clearly bounded conceptual domains (Typography/Tables, 2D Box Layout Engine, TeX Recursive Descent Parser, Delimiter Pre-processing/Span Scanning, and FTXUI Facade).
- **Hardening & Bug Fixes**: Identified and fixed 8 subtle edge cases and bugs, including document-level span search truncation, 2nd-dollar ambiguity, root-index erasure in display mode, dropped leading scripts, and streaming data loss on unclosed groups.
- **Test Infrastructure & KaTeX Reference**: Created a standalone unit test suite (`tests/latex_math_test.cc`) integrated into CMake/CTest, and added `math.md` featuring advanced KaTeX formulations (Standard Model Lagrangian, Lorenz Attractor, Schrödinger Equation, Scaled Dot-Product Attention).
- **Interface Preservation**: 100% backward-compatible with the public API in `include/ftxui/ext/latex_math.h` and existing consumers (`md_parser.cc`, `md_render.cc`).

---

## 2. Conceptual Separation of Concerns

The refactored implementation is organized into five distinct, decoupled layers within `src/latex_math.cc`:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        5. FTXUI Facade Layer                           │
│  render_math(source, style, mode)  →  ftxui::Element                   │
└───────────────────▲────────────────────────────────▲───────────────────┘
                    │                                │
┌───────────────────┴──────────────┐   ┌─────────────┴───────────────────┐
│ 3. TeX Grammar & Parser Engine   │   │ 4. Normalization & Span Scanner │
│  parse_math_sequence             │   │  NormalizeMathDelimiters        │
│  parse_math_group                │   │  find_math_span                 │
│  parse_math_matrix               │   │  substitute_inline_math         │
│  read_delim_token, split_top_lvl │   │  escape_math_content            │
└───────────────────▲──────────────┘   └─────────────────────────────────┘
                    │
┌───────────────────┴────────────────────────────────────────────────────┐
│                    2. Terminal 2D Box Layout Engine                    │
│  LayoutBox (height_above, height_below, rows, baseline alignment)      │
│  math_hrow (std::span<const LayoutBox>), math_hbox                     │
│  math_fraction, stack_limit, vstack_boxes, make_delimiter_column       │
└───────────────────────────────────▲────────────────────────────────────┘
                                    │
┌───────────────────────────────────┴────────────────────────────────────┐
│                  1. Typography & POD Symbol Tables                     │
│  SymbolEntry, ScriptEntry, DelimiterExt, EnvTraits (constexpr .rodata) │
│  kMathSymbols, kSuperScripts, kSubScripts, kSpacedOperators, kBigOps   │
│  Binary search lookups (std::lower_bound, std::binary_search)          │
└────────────────────────────────────────────────────────────────────────┘
```

### Layer Details:
1. **Typography & POD Tables**: Read-only, compile-time symbol definitions. No runtime memory allocations, no locks, no destructor registration.
2. **2D Box Layout Engine**: Geometric layout on the terminal character grid. Enforces the invariant:
   $$\text{total\_height} = \text{height\_above} + 1 + \text{height\_below}$$
   Supports linear and 2D vertical stacking, fraction bar rendering, horizontal concatenation with linear-time width caching, and bracket column stretching.
3. **TeX Parser & Grammar Engine**: Recursive descent scanner handling scripts (`^`, `_`), macros (`\frac`, `\sqrt`, `\binom`, `\substack`, accents, styles), environments (`matrix`, `pmatrix`, `cases`, `aligned`, `array`), and font alphabets (`\mathbb`, `\mathcal`, `\mathfrak`).
4. **Normalization & Span Scanner**: Markdown pre-pass turning `\(...\)` and `\[...\]` into `$...$` and `$$...$$`, escaping embedded punctuation to prevent markdown emphasis corruption, and isolating math spans.
5. **FTXUI Facade**: Translates `LayoutBox` character rows into FTXUI `Element` hierarchies (vertical box of text rows for display mode, single text element for inline mode).

---

## 3. Data-Driven POD Architecture & Decisions

### Why POD Types?
Previously, static `unordered_map<std::string, std::string>` and `unordered_set<std::string>` objects (`SvMap`, `SvSet`) were instantiated inside functions (`math_symbols()`, `script_glyphs()`, `is_spaced_operator()`, `is_big_operator()`, `read_delim_token()`, `linear_fraction()`). While `is_transparent` avoided `std::string` temporary copies on lookups, each table still incurred:
- Dynamic heap allocation of hundreds of bucket nodes during runtime initialization.
- Thread-safe magic static initialization guards on every probe.
- Pointer indirection and cache-unfriendly linked-list node traversal.

### POD Data Models
All tables were converted to immutable, sorted arrays of POD structs:
```cpp
struct SymbolEntry {
    std::string_view name;
    std::string_view glyph;
};
```
- **Lookup Cost**: Over ~180 entries, `std::lower_bound` performs at most $\lceil\log_2(180)\rceil = 8$ comparisons of `std::string_view`.
- **Memory Footprint**: All table data is stored in the binary's `.rodata` section.
- **Cache Locality**: Contiguous array access, eliminating heap pointer chasing.
- **Thread Safety**: Naturally re-entrant and thread-safe without mutexes or runtime init guards.

### Replaced Tables:
- `kMathSymbols`: ~180 Greek letters, arrows, relations, binary operators, functions.
- `kSuperScripts` / `kSubScripts`: Script character glyph mappings.
- `kVulgarFractions`: 15 precomposed Unicode fraction representations (`1/2` $\to$ `½`, etc.).
- `kSpacedOperators`: 60 relation and binary operators.
- `kBigOperators`: 17 display operators with stacked limits.
- `kAccents`: 15 combining diacritical marks replacing a 15-branch nested ternary.
- `kNamedDelimiters`: 13 delimiter macro mappings.
- `kExtDelimiters`: 12 bracket piece specifications (`top`, `mid`, `bottom`, `hook`).
- `kEnvTraits`: 17 LaTeX math environments (`pmatrix`, `bmatrix`, `cases`, `aligned`, etc.).

---

## 4. Bugs Identified and Fixed

During deep code analysis, eight specific issues were identified and resolved:

### Bug 1: `find_math_span` Early Exit on Unmatched Single `$`
- **Issue**: In `find_math_span`, when a line contained an unmatched `$` (e.g., currency `Paid $5 at lunch`), the scanner hit newline without finding a closing `$`, immediately returning `std::nullopt`. This aborted the search for the *entire remainder of the string*, silently ignoring all subsequent valid math spans on later lines (e.g., `\n$x + y = z$`).
- **Fix**: Replaced the premature abort with `scan_pos = open + 1; continue;`, allowing the scanner to advance past the unclosed `$` and discover subsequent math blocks.

### Bug 2: Ambiguous Single `$` Closing on Second Dollar of `$$`
- **Issue**: The inline `$` closing check only tested `close + 1 >= text.size() || text[close + 1] != '$'`. When encountering display math `$$x = 1$$` preceded by an unmatched `$`, the second `$` of `$$` was not followed by `$`, so the parser treated it as the closing delimiter of the unmatched single `$`, corrupting both spans.
- **Fix**: Added `not_preceded_by_dollar = (close == 0 || text[close - 1] != '$')` so single-dollar closes never latch onto any part of a double-dollar pair.

### Bug 3: `\sqrt[n]{...}` Display Mode Root Index Erasure
- **Issue**: In display mode `\sqrt[n]{...}`, `prefix` was initialized with `index + "√"`, but immediately overwritten by `prefix.rows.assign(..., "")` and `prefix.rows[prefix.height_above] = "√"`. The optional root index `[n]` was completely discarded in 2D display layouts.
- **Fix**: Preserved the index by formatting it as a superscript prefix (e.g., `³√` or `n√`) and retaining it when populating the radical column.

### Bug 4: Leading Script Drop When `pieces.empty()`
- **Issue**: When an expression began with a subscript or superscript (e.g. `^{238}\text{U}` for isotopes or `_{0}^{1}` for limits), `if (!pieces.empty())` evaluated to false. The script had no preceding atom to bind to and was silently discarded.
- **Fix**: If `pieces.empty()`, automatically initialize with an empty base atom `math_text("")` so leading scripts bind and render correctly.

### Bug 5: Streaming EOF Data Loss in `parse_math_group`
- **Issue**: If `input` reached EOF before encountering a closing `}` (which occurs routinely on every token during LLM response streaming), `parse_math_group` returned `std::nullopt`. Callers (`\frac`, `\text`, `{...}`) discarded the entire partially parsed contents, resulting in flickering/blank UI while the equation was in-flight.
- **Fix**: If EOF is reached without `}`, `parse_math_group` now gracefully returns the partially parsed `LayoutBox`, ensuring smooth token-by-token streaming rendering.

### Bug 6: `next_glyph` Buffer Over-read Guard
- **Issue**: `next_glyph` did not verify `pos < input.size()` before indexing `input[pos++]`. Calling it at EOF caused out-of-bounds access.
- **Fix**: Added early guard `if (pos >= input.size()) return {};`.

### Bug 7: Missing `\coloneqq` and `\coloneq` Symbols
- **Issue**: The symbol `≔` was present in `is_spaced_operator`, but missing from `math_symbols()`. Expressions like `A \coloneqq B` rendered as raw string `coloneqq`.
- **Fix**: Added `\coloneqq` and `\coloneq` mapping to `≔` in `kMathSymbols`.

### Bug 8: Missing `\limits` and `\nolimits` State Handling
- **Issue**: Directives `\limits` and `\nolimits` were silently consumed without updating operator state.
- **Fix**: `\limits` sets `prev_is_bigop = true`, and `\nolimits` sets `prev_is_bigop = false`, enabling constructs like `\int\limits_0^1` to stack limits in display mode.

### Bug 9: Dangling `**` When Bold/Italic Wraps Inline Math Spans
- **Issue**: In `src/md_parser.cc`, `parse_inlines` previously divided text at math span boundaries and passed each isolated chunk to `parse_emphasis`. When bold `**...**` or italic `*...*` wrapped a phrase or sentence containing an inline formula (e.g. `**In summary, the equation states that the field ( \(\phi\) ) must equal zero.**`), the opening `**` fell into the chunk before math and the closing `**` fell into the chunk after math. Neither chunk found its matching delimiter, causing both `**` markers to remain unmatched and be printed as raw `**` text in the UI.
- **Fix**: Replaced disjoint chunking with an integrated, context-aware inline parser `parse_inlines_into` with `find_closing_delim`. Delimiters outside math now match seamlessly across math and code boundaries, cleanly stripping the formatting markers, styling the surrounding prose as `MdInlineKind::Bold` or `MdInlineKind::Italic`, and marking the enclosed math as `emphasized = true`.

---

## 5. Performance & Streaming Enhancements

1. **Linear-Time Horizontal Layout (`math_hrow`)**:
   - Accepts `std::span<const LayoutBox>` rather than `const std::vector<LayoutBox>&`.
   - Pairwise combines (`math_hbox`) now use a stack-allocated array `const LayoutBox pair[2] = {left, right}` without heap allocation.
   - Eliminates redundant UTF-8 decoding during horizontal placement by propagating running column widths.
2. **Streaming Tolerance**:
   - `read_brace_group_raw` and `parse_math_group` now handle unterminated groups at EOF by recovering partial spans instead of failing.
   - Equation rendering remains stable and readable across intermediate streaming states.

---

## 6. KaTeX & LaTeX Math Verification Suite

- **`tests/latex_math_test.cc`**:
  - 101 comprehensive test assertions covering span detection, delimiter normalization, inline substitution, 2D display layout, bold/italic wrapping across math, and bug regression tests.
  - Added to `CMakeLists.txt` via `latex_math_test` executable, registered with CTest (`100% tests passed out of 1`).
- **`math.md`**:
  - Created reference test document including:
    1. Scaled Dot-Product & Multi-Head Attention: $\text{Attention}(Q,K,V) = \text{softmax}\left(\frac{QK^T}{\sqrt{d_k}}\right)V$
    2. Time-Dependent Schrödinger Equation: $i\hbar \frac{\partial \Psi}{\partial t} = \hat{H}\Psi$
    3. Lorenz Attractor 3D dynamical system in `aligned` environment
    4. Complete Standard Model Lagrangian: $\mathcal{L}_{\text{SM}} = \mathcal{L}_{\text{gauge}} + \mathcal{L}_{\text{fermion}} + \mathcal{L}_{\text{Higgs}} + \mathcal{L}_{\text{Yukawa}}$
    5. Einstein Field Equations with `\coloneqq`
    6. $3 \times 3$ Rotation Matrix and Piecewise Leaky ReLU `cases` environment
    7. Higher-order radical roots $\sqrt[3]{...} \le \sqrt[4]{...}$

---

## 7. Observations & Future Deferments

1. **`\middle` Vertical Stretching**:
   - *Observation*: `\middle|` currently inserts a single-height delimiter atom because `parse_math_sequence` performs a single left-to-right pass before the total height of the enclosing `\left...\right` expression is determined.
   - *Deferment*: Supporting stretched `\middle` delimiters would require a two-pass layout model (pass 1 to determine maximum child height, pass 2 to size intermediate delimiters). Deferred to maintain zero-overhead single-pass rendering.
2. **Matrix Border Separators in `array`**:
   - *Observation*: Vertical column separators (`|` in `array{c|c}`) and horizontal rules (`\hline`) are currently dropped in favor of clean space padding.
   - *Deferment*: Terminal columns are precious; full table grid borders inside inline/display math can push equations beyond viewport boundaries. Left as space-padded alignment for readability.
3. **Sub-character Kerning**:
   - *Observation*: Terminal emulators operate on discrete monospace character cells, so sub-pixel kerning and fractional font metric shifting are physically impossible without sixel/canvas raster graphics (which was previously tried and abandoned due to braille dot artifacts). Monospace character grid layout remains the optimal terminal presentation.

---

## 8. Addendum: Bold/Italic & Math Parsing Integration

### Problem Statement
When markdown formatting (bold `**...**` or italic `*...*` / `_..._`) wrapped a sentence or phrase that contained an inline math formula (such as `**In summary, the equation states that the field ( \(\phi\) ) must equal zero.**`), the terminal output left the raw `**` asterisks rendered literally around the text instead of bolding it.

### Root Cause
`src/md_parser.cc`'s `parse_inlines` previously claimed math spans first by splitting the raw text into disjoint chunks around each equation boundary:
- Chunk 1: prefix text up to the math span
- Chunk 2: math span
- Chunk 3: suffix text after the math span

Each chunk was passed independently to `parse_emphasis`. Consequently:
- The opening `**` was in Chunk 1, but its matching closing `**` was in Chunk 3.
- Chunk 1's scanner looked for closing `**` within Chunk 1, found none, and emitted the opening `**` as literal text.
- Chunk 3's scanner encountered a closing `**` with no opener, and emitted it as literal text.
- An existing check only handled `**$x$**` where `**` was directly adjacent to `$`, failing on any phrase with words, spaces, or parentheses surrounding the equation.

### Resolution
1. **Integrated Inline Parser**: Replaced disjoint chunking with `parse_inlines_into` and `find_closing_delim`.
2. **Span-Aware Delimiter Matching**: `find_closing_delim` searches forward across math spans, code blocks, and escaped characters, matching closing delimiters across equations.
3. **Format Context Propagation**:
   - Delimiter tokens (`**`, `*`, `_`) are cleanly consumed without leaking into the rendered text.
   - Text preceding and following the math formula is styled according to the active formatting context (`MdInlineKind::Bold` or `MdInlineKind::Italic`).
   - Enclosed math formulas are flagged with `emphasized = true` to render in `theme.math_emphasis` color.
4. **Intraword Underscore Protection**: Intraword underscores in prose (e.g. `variable_name`) are guarded to prevent accidental italicization.
5. **Regression Coverage**: Added `test_bold_math_interaction()` in `tests/latex_math_test.cc` asserting that `**` markers are never emitted literally when wrapping math expressions (101 unit tests passing).

