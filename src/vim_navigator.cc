#include "ftxui/ext/vim_navigator.h"

namespace ftxui::ext
{

  namespace
  {

    bool CharIs(const ftxui::Event &event, char c)
    {
      return event.is_character() && event.character().size() == 1 && event.character()[0] == c;
    }

  } // namespace

  std::optional<VimEvent> VimNavigator::feed(const ftxui::Event &event)
  {
    // The "z" fold-command chord takes precedence over everything else once
    // pending: the next key either completes a fold command or drops the
    // prefix, same "stale prefix aborts" rule the digit-count buffer follows.
    if (pending_z_prefix_)
    {
      pending_z_prefix_ = false;
      const int z_count = pending_count_ == 0 ? 1 : pending_count_;
      pending_count_ = 0;

      std::optional<VimAction> z_action;
      if (CharIs(event, 'a') || CharIs(event, 'z'))
      {
        z_action = VimAction::ToggleFold;
      }
      else if (CharIs(event, 'm'))
      {
        z_action = VimAction::CollapseAllFolds;
      }
      else if (CharIs(event, 'r') || CharIs(event, 'i'))
      {
        z_action = VimAction::ExpandAllFolds;
      }
      else if (CharIs(event, 'o'))
      {
        z_action = VimAction::OpenFold;
      }
      else if (CharIs(event, 'c'))
      {
        z_action = VimAction::CloseFold;
      }
      if (!z_action)
        return std::nullopt;
      return VimEvent{*z_action, z_count};
    }

    // <C-w> outranks the others for the same reason vim gives it a chord of
    // its own: "move to another window" must stay reachable no matter what
    // the current one thinks h/j/k/l mean.
    if (pending_ctrl_w_)
    {
      pending_ctrl_w_ = false;
      pending_count_ = 0;

      if (CharIs(event, 'h') || event == ftxui::Event::ArrowLeft)
        return VimEvent{VimAction::WindowLeft, 1};
      if (CharIs(event, 'j') || event == ftxui::Event::ArrowDown)
        return VimEvent{VimAction::WindowDown, 1};
      if (CharIs(event, 'k') || event == ftxui::Event::ArrowUp)
        return VimEvent{VimAction::WindowUp, 1};
      if (CharIs(event, 'l') || event == ftxui::Event::ArrowRight)
        return VimEvent{VimAction::WindowRight, 1};
      return std::nullopt;
    }

    if (event == ftxui::Event::CtrlW)
    {
      pending_ctrl_w_ = true;
      return std::nullopt;
    }

    if (pending_g_prefix_)
    {
      pending_g_prefix_ = false;
      const int g_count = pending_count_ == 0 ? 1 : pending_count_;
      pending_count_ = 0;

      if (CharIs(event, 'g'))
        return VimEvent{VimAction::GoDocumentStart, g_count};
      return std::nullopt;
    }

    if (CharIs(event, 'z'))
    {
      pending_z_prefix_ = true;
      return std::nullopt;
    }

    if (CharIs(event, 'g'))
    {
      pending_g_prefix_ = true;
      return std::nullopt;
    }

    // A bare leading '0' is vim's start-of-line motion, not a count digit
    // (counts start at 1; '0' only joins a count once digits are buffered,
    // e.g. "10j" — that path is below).
    if (CharIs(event, '0') && pending_count_ == 0)
    {
      return VimEvent{VimAction::Home, 1};
    }

    if (event.is_character() && event.character().size() == 1)
    {
      const char c = event.character()[0];
      if (c >= '0' && c <= '9')
      {
        pending_count_ = pending_count_ * 10 + (c - '0');
        return std::nullopt;
      }
    }

    const int count = pending_count_ == 0 ? 1 : pending_count_;
    pending_count_ = 0;

    std::optional<VimAction> action;
    if (CharIs(event, 'h'))
    {
      action = VimAction::CharLeft;
    }
    else if (CharIs(event, 'l'))
    {
      action = VimAction::CharRight;
    }
    else if (CharIs(event, 'w') || CharIs(event, 'W'))
    {
      action = VimAction::WordForward;
    }
    else if (CharIs(event, 'b') || CharIs(event, 'B'))
    {
      action = VimAction::WordBackward;
    }
    else if (CharIs(event, 'e') || CharIs(event, 'E'))
    {
      action = VimAction::WordEnd;
    }
    else if (CharIs(event, '}'))
    {
      action = VimAction::ParagraphForward;
    }
    else if (CharIs(event, '{'))
    {
      action = VimAction::ParagraphBackward;
    }
    else if (CharIs(event, 'j') || event == ftxui::Event::ArrowDown)
    {
      action = VimAction::LineDown;
    }
    else if (CharIs(event, 'k') || event == ftxui::Event::ArrowUp)
    {
      action = VimAction::LineUp;
    }
    else if (event == ftxui::Event::PageDown)
    {
      action = VimAction::LinePageDown;
    }
    else if (event == ftxui::Event::PageUp)
    {
      action = VimAction::LinePageUp;
    }
    else if (CharIs(event, 'G'))
    {
      action = VimAction::GoDocumentEnd;
    }
    else if (CharIs(event, '$'))
    {
      action = VimAction::End;
    }
    else if (CharIs(event, 'H'))
    {
      action = VimAction::ViewportTop;
    }
    else if (CharIs(event, 'M'))
    {
      action = VimAction::ViewportMiddle;
    }
    else if (CharIs(event, 'L'))
    {
      action = VimAction::ViewportBottom;
    }
    else if (CharIs(event, ':'))
    {
      action = VimAction::EnterCommandMode;
    }
    else if (CharIs(event, '/'))
    {
      return VimEvent{VimAction::SearchForward, 1};
    }
    else if (CharIs(event, 'n'))
    {
      action = VimAction::SearchNext;
    }
    else if (CharIs(event, 'N'))
    {
      action = VimAction::SearchPrevious;
    }
    else if (event == ftxui::Event::Return || CharIs(event, 'o'))
    {
      action = VimAction::Activate;
    }
    else if (event == ftxui::Event::CtrlE)
    {
      action = VimAction::Elaborate;
    }

    if (!action)
      return std::nullopt;
    return VimEvent{*action, count};
  }

} // namespace ftxui::ext
