#pragma once

#include "ftxui/ext/utf8.h"

#include <string>

namespace ftxui::ext {

// Pure text-position/motion logic over a UTF-8 std::string. No FTXUI
// dependency, no knowledge of rendered/wrapped layout — byte offsets only,
// matching FTXUI's own Input component's cursor model.
class UserTextCursor {
 public:
  void Reset() { byte_offset_ = 0; }
  void ClampTo(const std::string& text);

  int byte_offset() const { return byte_offset_; }
  // Seeds the cursor at an arbitrary offset. Callers that drive motions over
  // a rebuilt string (e.g. a freshly captured screen grid) set the position,
  // apply a motion, then read byte_offset() back. Not bounds-checked here;
  // every motion clamps before it moves.
  void set_byte_offset(int offset) { byte_offset_ = offset; }

  void MoveCharLeft(const std::string& text);
  void MoveCharRight(const std::string& text);
  void MoveWordForward(const std::string& text);
  void MoveWordBackward(const std::string& text);
  void MoveWordEnd(const std::string& text);
  // Moves to the start/end of the current line (0 / $). Lines are separated
  // by '\n', matching the VisibleGrid's row-joined text.
  void MoveLineStart(const std::string& text);
  void MoveLineEnd(const std::string& text);
  void MoveParagraphForward(const std::string& text);
  void MoveParagraphBackward(const std::string& text);

 private:
  int byte_offset_ = 0;
};

}  // namespace ftxui::ext
