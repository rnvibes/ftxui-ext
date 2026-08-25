#pragma once

#include <string>

namespace ftxui::ext {

// True for UTF-8 continuation bytes (top two bits == 10), i.e. the bytes
// that follow a glyph's lead byte.
bool utf8_is_continuation(char c);

// Steps one UTF-8 glyph forward/backward from byte_offset, skipping
// continuation bytes (top two bits == 10). Clamped to [0, text.size()].
int utf8_next_boundary(const std::string& text, int byte_offset);
int utf8_prev_boundary(const std::string& text, int byte_offset);

// Display columns the text occupies: one per glyph, two for the East Asian
// wide and emoji ranges a terminal draws double-width. Not a full wcwidth --
// it is enough to wrap a prompt without a line spilling past its border, which
// is the only thing that reads it.
int utf8_columns(const std::string& text);

// The byte offset whose column is `column`, clamped to the text. The inverse
// of utf8_columns over a prefix, so a caret can be placed back on the byte the
// wrapper counted.
int utf8_offset_for_column(const std::string& text, int column);

}  // namespace ftxui::ext
