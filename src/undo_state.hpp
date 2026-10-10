/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <sigc++/sigc++.h>

#include <cstdint>

namespace writeit {

// The undo history's state, as the dirty check sees it. STUB until PR 3
// (undo as recorded operations) replaces it with the real undo stack; only
// state_id() and signal_state_changed() are the agreed interface, and they
// are all the rest of the window may use.
//
// state_id() names the step at the top of the undo stack, 0 when it is
// empty. Every new step gets a fresh id, never reused. The signal fires
// after any push, undo, redo or clear.
//
// Typing, Delete or Backspace that joins the step on top gives that step a
// fresh id ("fresh id on merge"): "ab", Save, "c" is dirty, and one Undo
// still takes "abc" away.
//
// The stub has no steps of its own. The window calls bump() for every change
// to the buffer (insert, delete, tag applied or removed), so any change after
// a save gives a new id, a joined keystroke included. Undo and redo put back
// the id their snapshot was taken at (restore()), so undoing to the saved
// state gives the saved id again.
//
// A user action that inserted, deleted or re-tagged something is a step
// with a fresh id even if the text comes out the same (typing "c" over a
// selected "c"). One that did none of these (Backspace at the very start,
// Bold on bold text, whose tags the editor strips and puts back) adds no
// step and keeps the id it began with.
class Undo {
 public:
  std::uint64_t state_id() const
  {
    return id_;
  }
  sigc::signal<void()>& signal_state_changed()
  {
    return changed_;
  }

  // Stub only, for the window.
  // A change: a fresh id.
  void bump()
  {
    id_ = ++last_;
    changed_.emit();
  }
  // Undo or redo back to a state that had `id`.
  void restore(std::uint64_t id)
  {
    id_ = id;
    changed_.emit();
  }
  // The undo stack was emptied (New, Open).
  void clear()
  {
    id_ = 0;
    changed_.emit();
  }

 private:
  std::uint64_t id_ = 0;
  // The last id handed out; ids are never reused.
  std::uint64_t last_ = 0;
  sigc::signal<void()> changed_;
};

}  // namespace writeit
