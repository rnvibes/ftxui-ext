cmake_minimum_required(VERSION 3.21)

include(FetchContent)

set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

# Generic FTXUI extensions, shared across the tree (ragatui, tests, and any
# future FTXUI consumer). Terminal rendering primitives (scrollbar, cursor
# overlay, frame buffer, image views, the LED black hole), the LaTeX-to-
# terminal-Unicode math renderer, Tree-sitter code highlighting, and the pure
# text/input pieces that never belonged to the chat app (UTF-8 helpers, cursor
# motions, vim intent, the ticker, the profiler, file reading, the markdown
# pipeline). Nothing here may depend on the suri runtime -- suri-code is the
# one optional exception, for syntax highlighting.
add_library(ftxui-ext STATIC
    src/latex_math.cc
    src/cursor_overlay.cc
    src/screen_grid.cc
    src/frame_buffer.cc
    src/led_black_hole.cc
    src/image_decode.cc
    src/bitmap_view.cc
    src/gif_view.cc
    src/code_highlighter.cc
    src/utf8.cc
    src/user_text_cursor.cc
    src/vim_navigator.cc
    src/app_ticker.cc
    src/file_source.cc
    src/md/parser.cc
    src/md/render.cc
    src/org/render.cc
)
target_include_directories(ftxui-ext PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ftxui-ext PUBLIC dom component screen)

if(SURI_ENABLE_TREESITTER)
  target_link_libraries(ftxui-ext PUBLIC suri-code)
  target_compile_definitions(ftxui-ext PUBLIC SURI_CHAT_HAS_TREESITTER)
endif()
