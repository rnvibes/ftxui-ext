#include "ftxui/ext/user_text_cursor.h"

#include "ftxui/ext/utf8.h"

#include <algorithm>
#include <cctype>

namespace ftxui::ext {

namespace {

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

}  // namespace

void UserTextCursor::ClampTo(const std::string& text) {
  byte_offset_ = std::clamp(byte_offset_, 0, static_cast<int>(text.size()));
}

void UserTextCursor::MoveCharLeft(const std::string& text) {
  ClampTo(text);
  byte_offset_ = utf8_prev_boundary(text, byte_offset_);
}

void UserTextCursor::MoveCharRight(const std::string& text) {
  ClampTo(text);
  byte_offset_ = utf8_next_boundary(text, byte_offset_);
}

void UserTextCursor::MoveWordForward(const std::string& text) {
  ClampTo(text);
  const int size = static_cast<int>(text.size());
  int offset = byte_offset_;

  if (offset < size && !IsSpace(text[offset])) {
    while (offset < size && !IsSpace(text[offset])) ++offset;
  }
  while (offset < size && IsSpace(text[offset])) ++offset;

  byte_offset_ = offset;
}

void UserTextCursor::MoveWordBackward(const std::string& text) {
  ClampTo(text);
  int offset = byte_offset_;

  if (offset > 0) --offset;
  while (offset > 0 && IsSpace(text[offset])) --offset;
  while (offset > 0 && !IsSpace(text[offset - 1])) --offset;

  byte_offset_ = std::max(offset, 0);
}

void UserTextCursor::MoveWordEnd(const std::string& text) {
  ClampTo(text);
  const int size = static_cast<int>(text.size());
  if (size == 0) {
    byte_offset_ = 0;
    return;
  }

  int offset = byte_offset_;
  if (offset < size - 1) {
    ++offset;
  } else {
    byte_offset_ = size - 1;
    return;
  }

  while (offset < size && IsSpace(text[offset])) ++offset;
  if (offset >= size) {
    byte_offset_ = size - 1;
    return;
  }
  while (offset + 1 < size && !IsSpace(text[offset + 1])) ++offset;

  byte_offset_ = offset;
}

void UserTextCursor::MoveLineStart(const std::string& text) {
  ClampTo(text);
  // Start of the current line: just after the previous '\n', or 0. Already at
  // a line start (the byte right after '\n'), rfind finds that same '\n' and
  // the cursor stays put.
  if (byte_offset_ == 0) return;
  const std::size_t nl = text.rfind('\n', static_cast<std::size_t>(byte_offset_) - 1);
  byte_offset_ = nl == std::string::npos ? 0 : static_cast<int>(nl) + 1;
}

void UserTextCursor::MoveLineEnd(const std::string& text) {
  ClampTo(text);
  // End of the current line: the byte of the next '\n' (which the grid maps
  // to the row's last column), or the text end on the final line. Already at
  // a line end, find() returns that same '\n' and the cursor stays put.
  const std::size_t nl = text.find('\n', static_cast<std::size_t>(byte_offset_));
  byte_offset_ = nl == std::string::npos ? static_cast<int>(text.size())
                                         : static_cast<int>(nl);
}

void UserTextCursor::MoveParagraphForward(const std::string& text) {
  ClampTo(text);
  const int size = static_cast<int>(text.size());
  std::size_t blank = text.find("\n\n", byte_offset_);
  if (blank == std::string::npos) {
    byte_offset_ = size;
    return;
  }
  int offset = static_cast<int>(blank);
  while (offset < size && IsSpace(text[offset])) ++offset;
  byte_offset_ = offset;
}

void UserTextCursor::MoveParagraphBackward(const std::string& text) {
  ClampTo(text);
  if (byte_offset_ == 0) return;

  // If sitting right at the start of a paragraph (immediately after a blank
  // run), step back over that blank run first so repeated presses walk to
  // the previous paragraph instead of re-finding the same break.
  int adjusted = byte_offset_;
  while (adjusted > 0 && text[adjusted - 1] == '\n') --adjusted;

  std::size_t blank = std::string::npos;
  if (adjusted > 0) {
    blank = text.rfind("\n\n", static_cast<std::size_t>(adjusted) - 1);
  }
  if (blank == std::string::npos) {
    byte_offset_ = 0;
    return;
  }
  int offset = static_cast<int>(blank) + 2;
  while (offset < static_cast<int>(text.size()) && IsSpace(text[offset])) ++offset;
  byte_offset_ = offset;
}

}  // namespace ftxui::ext
