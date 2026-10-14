/* SPDX-License-Identifier: Unlicense */

// A footnote taller than the sheet moves the line even when the line is
// taller than the room the note left. A note that fits keeps the old test.
// The painted note is clipped to one page of text.

#include "check.hpp"
#include "view.hpp"

namespace {

using writeit::clamp_note_height;
using writeit::line_moves_for_note;

void moves()
{
  // The note does not fit. The line is taller than the 1 px room and still moves.
  CHECK(line_moves_for_note(20, 1, 800, 692));
  CHECK(line_moves_for_note(20, 1, 692, 692));
  // A note that fits moves a line only when the line fits in the room.
  CHECK(line_moves_for_note(20, 592, 100, 692));
  CHECK(!line_moves_for_note(700, 592, 100, 692));
  // No note is the old comparison against the whole page.
  CHECK(line_moves_for_note(20, 692, 0, 692));
  CHECK(!line_moves_for_note(800, 692, 0, 692));
}

void clamped()
{
  CHECK(clamp_note_height(800, 692) == 692);
  CHECK(clamp_note_height(100, 692) == 100);
  CHECK(clamp_note_height(692, 692) == 692);
  CHECK(clamp_note_height(0, 692) == 0);
}

}  // namespace

constexpr int kChecks = 10;

int main()
{
  moves();
  clamped();
  return suite_test::done("tall-footnote", kChecks);
}
