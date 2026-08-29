# ftxui-ext

Generic extensions for [FTXUI](https://github.com/ArthurSonzogni/FTXUI), the C++
terminal UI library. This is the shared, app-agnostic layer behind terminal
apps: document rendering (Markdown and Org-mode), LaTeX math, syntax
highlighting, image/GIF viewing, vim navigation, UTF-8 text editing, file
utilities, and a handful of terminal widgets. I made it for [suri/ragatui](https://github.com/rnvibes/suri.git).

Nothing here depends on a chat runtime or any particular application — these
are pieces a terminal UI can compose.

## Features

- **Markdown pipeline** — a line-oriented parser covering ATX headings, fenced
  code, paragraphs, lists, blockquotes, tables (GFM), rules, and inline
  bold/italic/code/math, plus a pure document → rows renderer that wraps prose,
  lays out tables, and draws bordered syntax-highlighted code boxes.
  Tolerant of truncation by design: it is re-run on every streamed token.

- **Org-mode rendering** — an `OrgDocument` sharing the same block shapes as
  the Markdown document, with org's component colors (headlines, TODO/DONE,
  priorities, timestamps, drawers, planning lines, clocks, `#+BEGIN_SRC`/`QUOTE`/
  `EXAMPLE` blocks) and source-faithful spacing.

- **LaTeX math** — a `$...$`/`$$...$$` → terminal-Unicode renderer with
  fractions, sub/superscripts and matrices in display mode; inline math stays a
  single row. Includes `NormalizeMathDelimiters` to rewrite `\(...\)`/`\[...\]`
  and re-wrap bare `\begin{env}` blocks that LLM output routinely drops
  delimiters around.

- **Syntax highlighting** — Tree-sitter based (via the optional `suri-code`
  dependency), with a `SyntaxStyle` you can point at either theme.

- **Images & GIFs** — vendored `stb_image` decoding (PNG/JPEG/BMP/WebP/GIF),
  pannable/zoomable `BitmapView`/`PNGView`, and an animated `GIFView`, all
  rendered through a `TFrameBuffer` that maps logical pixels to terminal cells
  in braille or block mode.

- **Vim navigation** — `VimNavigator` translates raw keys into typed
  `VimAction`s (motions with count prefixes, `z` fold chords, `gg`/`G`,
  `H`/`M`/`L` viewport motions, `<C-w>` window switching), and
  `UserTextCursor` implements the byte-offset motions over UTF-8 text.

- **Terminal widgets** — a solid, mouse-addressable, draggable `Scrollbar`;
  a `CursorOverlay` with an optional cursorline that highlights wrapped units
  as one thing; `VisibleGrid` screen capture for hit-testing rendered output.

- **Utilities** — UTF-8 helpers (glyph stepping, display columns), sentence
  and context extraction for RAG-style follow-ups, bounded recursive file
  walks and text previews, an allocation-free `PerformanceStats` profiler, and
  an `AppTicker` background loop.

## Requirements

- CMake 3.21+
- A C++20 compiler
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v6.1.9 (fetched
  automatically via `FetchContent`)
- Optionally, `suri-code` for Tree-sitter syntax highlighting (enable with
  `SURI_ENABLE_TREESITTER`)

## Usage

`ftxui-ext` is a static library target. Link it from your CMake project:

```cmake
add_subdirectory(third_party/ftxui-ext)   # or FetchContent it
target_link_libraries(my_app PRIVATE ftxui-ext)
```

Headers live under `include/ftxui/ext/...` and are namespaced `ftxui::ext`.

### Quick example

```cpp
#include <ftxui/ext/md/parser.h>
#include <ftxui/ext/md/render.h>
#include <ftxui/ext/md/theme.h>

auto doc    = ftxui::ext::parse_markdown(markdown_source);
auto theme  = ftxui::ext::MdTheme::Dark();
auto rows   = ftxui::ext::render_markdown_rows(doc, theme, viewport_width);
auto screen = ftxui::Screen::Create(...);
Render(screen, ftxui::ext::render_markdown(doc, theme, viewport_width));
```

The renderers are pure document → rows: they carry no viewport or hit-testing
state, so the caller owns scrolling and can map a screen row back to the block
under it via each row's `block` index.

## Layout

```
include/ftxui/ext/
  md/                 markdown parser, renderer, theme
  org/                org-mode document, renderer, theme
  document.h          shared arena-backed document spine (zero-copy views)
  latex_math.h        LaTeX → terminal-Unicode math renderer
  text_layout.h       shared word-wrap, table-column sizing, code-box drawing
  code_highlighter.h  tree-sitter syntax highlighting
  context_extractor.h sentence/context extraction for markdown blocks
  bitmap_view.h       pannable/zoomable image component
  gif_view.h          animated GIF component
  frame_buffer.h      logical-pixel → terminal-cell frame buffer
  image_decode.h      stb_image decoding
  led_black_hole.h    animated black-hole rendering demo
  scrollbar.h         solid, draggable scrollbar
  cursor_overlay.h    cursor + cursorline overlay
  screen_grid.h       visible-grid capture
  utf8.h              UTF-8 boundary/column helpers
  user_text_cursor.h  byte-offset text motions
  vim_navigator.h     key → vim-action translation
  app_ticker.h        background timing loop
  file_source.h       directory walks and text previews
  performance_stats.h allocation-free timing profiler
```

## License

[AGPL-3.0](LICENSE)
