#include "ftxui/ext/vim_navigator.h"

#include <ftxui/component/event.hpp>

#include <cassert>
#include <iostream>

using namespace ftxui::ext;
using namespace ftxui;

void TestBasicMotions() {
  VimNavigator nav;

  auto ev = nav.feed(Event::Character('j'));
  assert(ev.has_value());
  assert(ev->action == VimAction::LineDown);
  assert(ev->count == 1);

  ev = nav.feed(Event::Character('k'));
  assert(ev.has_value());
  assert(ev->action == VimAction::LineUp);
  assert(ev->count == 1);

  ev = nav.feed(Event::Character('h'));
  assert(ev.has_value() && ev->action == VimAction::CharLeft && ev->count == 1);

  ev = nav.feed(Event::Character('l'));
  assert(ev.has_value() && ev->action == VimAction::CharRight && ev->count == 1);

  ev = nav.feed(Event::Character('0'));
  assert(ev.has_value() && ev->action == VimAction::Home && ev->count == 1);

  ev = nav.feed(Event::Character('^'));
  assert(ev.has_value() && ev->action == VimAction::FirstNonBlank && ev->count == 1);

  ev = nav.feed(Event::Character('$'));
  assert(ev.has_value() && ev->action == VimAction::End && ev->count == 1);

  ev = nav.feed(Event::CtrlD);
  assert(ev.has_value() && ev->action == VimAction::HalfPageDown && ev->count == 1);

  ev = nav.feed(Event::CtrlU);
  assert(ev.has_value() && ev->action == VimAction::HalfPageUp && ev->count == 1);

  ev = nav.feed(Event::CtrlF);
  assert(ev.has_value() && ev->action == VimAction::LinePageDown && ev->count == 1);

  ev = nav.feed(Event::CtrlB);
  assert(ev.has_value() && ev->action == VimAction::LinePageUp && ev->count == 1);

  ev = nav.feed(Event::Return);
  assert(ev.has_value() && ev->action == VimAction::Activate && ev->count == 1);

  ev = nav.feed(Event::Character('o'));
  assert(ev.has_value() && ev->action == VimAction::Activate && ev->count == 1);

  ev = nav.feed(Event::CtrlK);
  assert(ev.has_value() && ev->action == VimAction::LineUp && ev->count == 1);
}

void TestCounts() {
  VimNavigator nav;

  // "5j"
  auto ev = nav.feed(Event::Character('5'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('j'));
  assert(ev.has_value() && ev->action == VimAction::LineDown && ev->count == 5);

  // "12w"
  ev = nav.feed(Event::Character('1'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('2'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('w'));
  assert(ev.has_value() && ev->action == VimAction::WordForward && ev->count == 12);

  // "0" when count is 1 becomes 10
  ev = nav.feed(Event::Character('1'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('0'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('k'));
  assert(ev.has_value() && ev->action == VimAction::LineUp && ev->count == 10);
}

void TestGChords() {
  VimNavigator nav;

  // "gg"
  auto ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('g'));
  assert(ev.has_value() && ev->action == VimAction::GoDocumentStart && ev->count == 1);

  // "5gg"
  ev = nav.feed(Event::Character('5'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('g'));
  assert(ev.has_value() && ev->action == VimAction::GoDocumentStart && ev->count == 5);

  // "go" (open preview / silent)
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('o'));
  assert(ev.has_value() && ev->action == VimAction::OpenPreview && ev->count == 1);

  // "gi" (open split silent)
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('i'));
  assert(ev.has_value() && ev->action == VimAction::OpenSplitSilent && ev->count == 1);

  // "gs" (open vsplit silent)
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('s'));
  assert(ev.has_value() && ev->action == VimAction::OpenVSplitSilent && ev->count == 1);

  // "gt" (tab next)
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('t'));
  assert(ev.has_value() && ev->action == VimAction::TabNext && ev->count == 1);

  // "ge" (word end backward)
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('e'));
  assert(ev.has_value() && ev->action == VimAction::WordEndBackward && ev->count == 1);
}

void TestWindowChords() {
  VimNavigator nav;

  // <C-w>h
  auto ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('h'));
  assert(ev.has_value() && ev->action == VimAction::WindowLeft);

  // <C-w>w (cycle)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('w'));
  assert(ev.has_value() && ev->action == VimAction::WindowCycle);

  // <C-w><C-w> (cycle)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::CtrlW);
  assert(ev.has_value() && ev->action == VimAction::WindowCycle);

  // <C-w>p (previous)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('p'));
  assert(ev.has_value() && ev->action == VimAction::WindowPrevious);

  // <C-w>c / <C-w>q (close)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('c'));
  assert(ev.has_value() && ev->action == VimAction::WindowClose);

  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('q'));
  assert(ev.has_value() && ev->action == VimAction::WindowClose);

  // <C-w>s (split)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('s'));
  assert(ev.has_value() && ev->action == VimAction::WindowSplitHorizontal);

  // <C-w>v (vsplit)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('v'));
  assert(ev.has_value() && ev->action == VimAction::WindowSplitVertical);

  // <C-w>o (only/maximize)
  ev = nav.feed(Event::CtrlW);
  assert(!ev.has_value());
  ev = nav.feed(Event::Character('o'));
  assert(ev.has_value() && ev->action == VimAction::WindowOnly);
}

void TestEscape() {
  VimNavigator nav;

  // Escape cancels pending count
  auto ev = nav.feed(Event::Character('5'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Escape);
  assert(!ev.has_value());

  // Following key has no pending count
  ev = nav.feed(Event::Character('j'));
  assert(ev.has_value() && ev->count == 1);

  // Escape cancels pending g
  ev = nav.feed(Event::Character('g'));
  assert(!ev.has_value());
  ev = nav.feed(Event::Escape);
  assert(!ev.has_value());

  // Escape when nothing pending emits Escape action
  ev = nav.feed(Event::Escape);
  assert(ev.has_value() && ev->action == VimAction::Escape && ev->count == 1);
}

int main() {
  TestBasicMotions();
  TestCounts();
  TestGChords();
  TestWindowChords();
  TestEscape();
  std::cout << "All VimNavigator tests passed successfully!\n";
  return 0;
}
