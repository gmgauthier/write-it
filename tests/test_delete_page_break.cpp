/* SPDX-License-Identifier: Unlicense */

// Delete of the only character on an empty page-break paragraph, the pad,
// clears the break. A letter, a pad that is not alone, or a pad when the
// break is already off, does not.

#include "check.hpp"
#include "main_window.hpp"

constexpr int kChecks = 4;

int main()
{
  const gunichar pad = 0x200B;
  const gunichar letter = static_cast<gunichar>('a');
  CHECK(writeit::deleting_pad_clears_break(pad, true, true));
  CHECK(!writeit::deleting_pad_clears_break(letter, true, true));
  CHECK(!writeit::deleting_pad_clears_break(pad, false, true));
  CHECK(!writeit::deleting_pad_clears_break(pad, true, false));
  return suite_test::done("delete-page-break", kChecks);
}
