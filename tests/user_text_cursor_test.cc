#include "ftxui/ext/user_text_cursor.h"
#include "ftxui/ext/utf8.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ftxui::ext;

void TestUtf8Functions() {
  std::string s = "Hello, 世界! 🚀";
  // "Hello, " is 7 bytes, 7 columns
  // "世界" is 6 bytes (3 bytes each), 4 columns (2 columns each)
  // "! " is 2 bytes, 2 columns
  // "🚀" is 4 bytes, 2 columns
  // Total bytes: 7 + 6 + 2 + 4 = 19 bytes
  // Total columns: 7 + 4 + 2 + 2 = 15 columns

  assert(utf8_columns(s) == 15);

  int offset = 0;
  offset = utf8_next_boundary(s, offset); // 'H' -> 1
  assert(offset == 1);
  offset = utf8_prev_boundary(s, offset);
  assert(offset == 0);

  // Jump to '世' at byte 7
  int next_glyph = utf8_next_boundary(s, 7);
  assert(next_glyph == 10); // 3 bytes for '世'
  int prev_glyph = utf8_prev_boundary(s, 10);
  assert(prev_glyph == 7);
}

void TestCursorMotions() {
  UserTextCursor cursor;
  std::string text = "first line\nsecond line\n\nfourth paragraph";

  cursor.Reset();
  assert(cursor.byte_offset() == 0);

  cursor.MoveCharRight(text);
  assert(cursor.byte_offset() == 1);

  cursor.MoveWordForward(text);
  assert(cursor.byte_offset() == 6); // start of "line"

  cursor.MoveLineEnd(text);
  assert(cursor.byte_offset() == 10); // '\n'

  cursor.MoveLineStart(text);
  assert(cursor.byte_offset() == 0);

  cursor.MoveParagraphForward(text);
  assert(cursor.byte_offset() == 24); // start of "fourth"

  cursor.MoveParagraphBackward(text);
  assert(cursor.byte_offset() == 0);
}

int main() {
  TestUtf8Functions();
  TestCursorMotions();
  std::cout << "All UTF8 and UserTextCursor tests passed successfully!\n";
  return 0;
}
