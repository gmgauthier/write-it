/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "main_window.hpp"

namespace {

// A picture or a table anchor is U+FFFC in the buffer slice. Find has to
// count it, or the match after it selects the object and drops the last letter.
void picture_shifts_the_index()
{
  Glib::ustring hay("Hello ");
  hay.push_back(static_cast<gunichar>(0xFFFC));
  hay.append("world");
  const writeit::FindHit world = writeit::find_in_text(hay, "world", 0, true);
  const writeit::FindHit dropped = writeit::find_in_text("Hello world", "world", 0, true);
  CHECK(world.index == 7);
  CHECK(world.length == 5);
  CHECK(dropped.index == 6);
  CHECK(world.index == dropped.index + 1);
  const writeit::FindHit hello = writeit::find_in_text(hay, "Hello", 0, true);
  CHECK(hello.index == 0);
  CHECK(hello.length == 5);
}

void wrap_and_case()
{
  Glib::ustring hay("Hello ");
  hay.push_back(static_cast<gunichar>(0xFFFC));
  hay.append("world");
  const writeit::FindHit wrapped = writeit::find_in_text(hay, "Hello", 7, true);
  CHECK(wrapped.index == 0);
  const writeit::FindHit folded = writeit::find_in_text(hay, "WORLD", 0, false);
  CHECK(folded.index == 7);
  CHECK(folded.length == 5);
  const writeit::FindHit none = writeit::find_in_text(hay, "zzz", 0, true);
  CHECK(none.index < 0);
  const writeit::FindHit empty = writeit::find_in_text(hay, "", 0, true);
  CHECK(empty.index < 0);
}

}  // namespace

constexpr int kChecks = 11;

int main()
{
  picture_shifts_the_index();
  wrap_and_case();
  return suite_test::done("find-embedded", kChecks);
}
