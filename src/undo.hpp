/* SPDX-License-Identifier: Unlicense */

#pragma once

// Undo and redo as recorded operations. Every change the buffer makes inside
// an undo step (text inserted, text erased with its tags, a tag applied or
// removed over a range) is recorded as it happens, small, from the buffer's
// own signals; undo plays a step's operations back in reverse, each one
// inverted, and redo plays them forward again. Nothing copies the document.
//
// State the buffer cannot hold (the empty last paragraph's format, the style
// sheet) is recorded by the window as a custom operation with its own undo
// and redo.

#include <gtkmm.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace writeit {

class UndoHistory {
 public:
  // Tags the history never records: screen-only tags that the window
  // recomputes after every change (list-shift, list-tab).
  using IgnoreTag = std::function<bool(const Glib::RefPtr<Gtk::TextTag>&)>;

  // What a closed step did, for coalescing: a burst of typing, of Backspace
  // or of Delete is one step.
  enum class Shape { Other, Insertion, Backspace, Delete };
  enum class Closed { Dropped, Pushed, Merged };

  static constexpr std::size_t kCap = 200;

  UndoHistory();
  ~UndoHistory();
  UndoHistory(const UndoHistory&) = delete;
  UndoHistory& operator=(const UndoHistory&) = delete;

  void attach(const Glib::RefPtr<Gtk::TextBuffer>& buffer, IgnoreTag ignore);
  void detach();

  // --- The saved-state hook. ---
  // Names the document state the history is at. Every new step, and every
  // edit coalesced into the top step, gets a fresh id; undo and redo move
  // back and forth between ids already given; clear() (a new or loaded
  // document) starts from a fresh one. So a window that keeps state_id() at
  // its last save is back at its saved state exactly when they are equal.
  std::uint64_t state_id() const;
  // Emitted whenever state_id() may have changed: a step pushed or merged,
  // undo, redo, clear().
  sigc::signal<void>& signal_state_changed()
  {
    return state_changed_;
  }

  // Forgets every step: a new or loaded document.
  void clear();
  // Undo steps; size() for the tests that count them.
  std::size_t size() const
  {
    return undo_.size();
  }
  bool empty() const
  {
    return undo_.empty();
  }
  bool can_undo() const
  {
    return !undo_.empty();
  }
  bool can_redo() const
  {
    return !redo_.empty();
  }

  // Opens a step. `caret` is where undo puts the caret back; `selection`
  // whether text was selected (an edit over a selection never coalesces).
  // Opening a step forgets the redo steps, as the snapshot undo did.
  void open(int caret, bool selection);
  bool is_open() const
  {
    return open_;
  }
  // A change outside the buffer, as part of the open step. `seen` false: the
  // window's own state only, not an edit on its own (see close()).
  void record_custom(std::function<void()> undo, std::function<void()> redo, bool seen = true);
  // Closes the open step. A step that is no edit (no text inserted or
  // deleted, every character's tags as before, no seen custom change) adds
  // no step: its ops join the step below and state_id() stays. Text erased
  // and put back, as typing or pasting the same text over a selection, IS
  // an edit and its own step (Greg's rule). With `may_merge` (typing
  // inside the coalescing window) a step that continues the top step's
  // insertion, Backspace run or Delete run joins it, under a fresh id.
  Closed close(int caret, bool may_merge);

  // Plays the top step back. `caret` is where the caret is now, for redo to
  // put back. Returns where the caret goes, or -1 with nothing to undo.
  int undo(int caret);
  int redo();
  bool replaying() const
  {
    return replaying_;
  }

  // Buffer edits made outside any step while the history held steps. Each
  // one clears the history, which could no longer be played back; the tests
  // check that the window makes none.
  long stray_edits() const
  {
    return stray_;
  }
  // Operations in the top undo step, for the tests.
  std::size_t top_ops() const;
  // The top undo step's operations, one per line, for test failures.
  std::string describe_top() const;

 private:
  struct Op;
  struct Step;

  bool recording();
  void push_op(Op&& op);
  void on_insert(const Gtk::TextIter& pos, const Glib::ustring& text);
  void on_erase(const Gtk::TextIter& start, const Gtk::TextIter& end);
  void on_tag(const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
              const Gtk::TextIter& end, bool apply);
  bool is_edit(const Step& step) const;
  void note_tag_shape(const std::vector<std::pair<int, int>>& ranges);
  void play(Step& step, bool forward);
  void emit()
  {
    state_changed_.emit();
  }

  Glib::RefPtr<Gtk::TextBuffer> buffer_;
  IgnoreTag ignore_;
  std::vector<sigc::connection> connections_;
  std::vector<std::unique_ptr<Step>> undo_;
  std::vector<std::unique_ptr<Step>> redo_;
  std::unique_ptr<Step> current_;
  bool open_ = false;
  bool replaying_ = false;
  std::uint64_t next_id_ = 2;
  std::uint64_t base_id_ = 1;
  long stray_ = 0;
  sigc::signal<void> state_changed_;
};

}  // namespace writeit
