/* SPDX-License-Identifier: Unlicense */

// A custom step does not merge through close(). fold_last_custom replaces
// the redo of the step already closed, so a run of cell or story keys can
// be one undo. Playback of a custom-only step needs no text buffer.

#include "check.hpp"
#include "undo.hpp"

constexpr int kChecks = 16;

int main()
{
  int value = 0;
  writeit::UndoHistory history;
  history.open(0, false);
  history.record_custom([&] { value = 1; }, [&] { value = 2; });
  CHECK(history.close(0, false) == writeit::UndoHistory::Closed::Pushed);
  CHECK(history.fold_last_custom([&] { value = 3; }));
  CHECK(history.undo() == 0);
  CHECK(value == 1);
  CHECK(history.redo() == 0);
  CHECK(value == 3);

  writeit::UndoHistory empty;
  CHECK(!empty.fold_last_custom([&] { value = 0; }));
  empty.open(0, false);
  CHECK(!empty.fold_last_custom([&] { value = 0; }));

  // The next key has already opened a step. Fold still rewrites the closed
  // one, and closing the empty step leaves that one step.
  value = 0;
  writeit::UndoHistory burst;
  burst.open(0, false);
  burst.record_custom([&] { value = 1; }, [&] { value = 2; });
  CHECK(burst.close(0, false) == writeit::UndoHistory::Closed::Pushed);
  burst.open(0, false);
  CHECK(burst.fold_last_custom([&] { value = 4; }));
  CHECK(burst.close(0, false) == writeit::UndoHistory::Closed::Dropped);
  CHECK(burst.size() == 1);
  CHECK(burst.undo() == 0);
  CHECK(value == 1);
  CHECK(burst.redo() == 0);
  CHECK(value == 4);
  return suite_test::done("undo-fold", kChecks);
}
