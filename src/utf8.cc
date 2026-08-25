#include "ftxui/ext/utf8.h"

#include <algorithm>

namespace ftxui::ext {

namespace {

bool IsContinuationByte(char c) {
  return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Decodes the glyph starting at `offset` far enough to know its width.
int CodepointAt(const std::string& text, int offset) {
  const unsigned char lead = static_cast<unsigned char>(text[offset]);
  const int size = static_cast<int>(text.size());
  const auto tail = [&](int i) {
    return i < size ? (static_cast<unsigned char>(text[i]) & 0x3F) : 0;
  };
  if (lead < 0x80) return lead;
  if ((lead & 0xE0) == 0xC0) return ((lead & 0x1F) << 6) | tail(offset + 1);
  if ((lead & 0xF0) == 0xE0)
    return ((lead & 0x0F) << 12) | (tail(offset + 1) << 6) | tail(offset + 2);
  return ((lead & 0x07) << 18) | (tail(offset + 1) << 12) |
         (tail(offset + 2) << 6) | tail(offset + 3);
}

// The ranges a terminal renders two cells wide. Coarse on purpose: getting CJK
// and emoji right covers everything this app puts in a prompt, and a full
// East_Asian_Width table would be more code than the feature.
bool IsWide(int cp) {
  return (cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
         (cp >= 0x2E80 && cp <= 0xA4CF) ||   // CJK radicals .. Yi
         (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
         (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility ideographs
         (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK compatibility forms
         (cp >= 0xFF00 && cp <= 0xFF60) ||   // fullwidth forms
         (cp >= 0xFFE0 && cp <= 0xFFE6) ||
         (cp >= 0x1F300 && cp <= 0x1FAFF);   // emoji
}

}  // namespace

bool utf8_is_continuation(char c) { return IsContinuationByte(c); }

int utf8_next_boundary(const std::string& text, int byte_offset) {
  const int size = static_cast<int>(text.size());
  int offset = std::clamp(byte_offset, 0, size);
  if (offset >= size) return size;
  ++offset;
  while (offset < size && IsContinuationByte(text[offset])) ++offset;
  return offset;
}

int utf8_prev_boundary(const std::string& text, int byte_offset) {
  const int size = static_cast<int>(text.size());
  int offset = std::clamp(byte_offset, 0, size);
  if (offset <= 0) return 0;
  --offset;
  while (offset > 0 && IsContinuationByte(text[offset])) --offset;
  return offset;
}

int utf8_columns(const std::string& text) {
  int columns = 0;
  int offset = 0;
  const int size = static_cast<int>(text.size());
  while (offset < size) {
    columns += IsWide(CodepointAt(text, offset)) ? 2 : 1;
    offset = utf8_next_boundary(text, offset);
  }
  return columns;
}

int utf8_offset_for_column(const std::string& text, int column) {
  int columns = 0;
  int offset = 0;
  const int size = static_cast<int>(text.size());
  while (offset < size && columns < column) {
    columns += IsWide(CodepointAt(text, offset)) ? 2 : 1;
    offset = utf8_next_boundary(text, offset);
  }
  return offset;
}

}  // namespace ftxui::ext
