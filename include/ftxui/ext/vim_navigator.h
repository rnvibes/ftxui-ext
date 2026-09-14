#pragma once

#include <ftxui/component/event.hpp>

#include <optional>

namespace ftxui::ext
{

  enum class VimAction
  {
    CharLeft,
    CharRight,
    WordForward,
    WordBackward,
    WordEnd,
    ParagraphForward,
    ParagraphBackward,
    LineDown,         // j / ArrowDown
    LineUp,           // k / ArrowUp
    LinePageDown,     // PageDown
    LinePageUp,       // PageUp
    Home,             // 0 — start of the current line
    End,              // $ — end of the current line
    GoDocumentStart,  // gg
    GoDocumentEnd,    // G
    ViewportTop,      // H
    ViewportMiddle,   // M
    ViewportBottom,   // L
    ToggleFold,       // za / zz
    CollapseAllFolds, // zm
    ExpandAllFolds,   // zr / zi
    OpenFold,         // zo
    CloseFold,        // zc
    EnterCommandMode, // : — hands off to the app's `:command arg` line
    SearchForward,    // / — hands off to the view/app's `/search` line
    SearchNext,       // n — repeat search forward
    SearchPrevious,   // N — repeat search backward
    Activate,         // Enter / o — open whatever the cursor is on
    Elaborate,        // <C-e> — contextual inquiry / follow-up
    // <C-w>h/j/k/l — move focus between tiles. Vim's window vocabulary, and
    // the reason Tab is gone: a tiled layout has directions, and a ring does
    // not, so cycling was always a worse answer to "go left".
    WindowLeft,
    WindowDown,
    WindowUp,
    WindowRight,
  };

  struct VimEvent
  {
    VimAction action;
    int count = 1; // resolved repeat count; always >= 1
  };

  // Recognizes normal-mode navigation keys, an optional leading digit-string
  // count prefix (vim style: "5j", "3w"), the "z" fold-command chord (vim
  // style: "za", "zm", ...), the "g" chord ("gg" only, for now), and vim's
  // <C-w> window chord. Feed() is
  // called once per keyboard Event while in normal mode; it either buffers
  // pending input (a count digit or a "z"/"g" prefix — returns nullopt),
  // completes a motion/command (returns a VimEvent with the buffered count
  // resolved, then resets all pending state), or drops stale pending state on
  // any other key and returns nullopt. Pure key->intent translation: no
  // knowledge of chat-tui's turns/sessions/cursor state.
  class VimNavigator
  {
  public:
    std::optional<VimEvent> feed(const ftxui::Event &event);
    void ResetPending()
    {
      pending_count_ = 0;
      pending_z_prefix_ = false;
      pending_g_prefix_ = false;
      pending_ctrl_w_ = false;
    }

  private:
    int pending_count_ = 0; // 0 = no digits buffered yet
    bool pending_z_prefix_ = false;
    bool pending_g_prefix_ = false;
    bool pending_ctrl_w_ = false;
  };

} // namespace ftxui::ext
