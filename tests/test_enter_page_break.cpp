/* SPDX-License-Identifier: Unlicense */

// Enter ends a list on an empty item. A page-break pad (U+200B) is not text,
// so an item of only those pads is empty. Real text is not.

#include "check.hpp"
#include "main_window.hpp"

namespace {

Glib::ustring pad(int count)
{
  Glib::ustring text;
  for (int i = 0; i < count; ++i)
    text.push_back(static_cast<gunichar>(0x200B));
  return text;
}

}  // namespace

constexpr int kChecks = 7;

int main()
{
  CHECK(writeit::paragraph_is_blank_item(""));
  CHECK(writeit::paragraph_is_blank_item("\n"));
  CHECK(writeit::paragraph_is_blank_item(pad(1)));
  CHECK(writeit::paragraph_is_blank_item(pad(3)));
  CHECK(writeit::paragraph_is_blank_item(pad(1) + "\n"));
  CHECK(!writeit::paragraph_is_blank_item("a"));
  CHECK(!writeit::paragraph_is_blank_item("a\n"));
  return suite_test::done("enter-page-break", kChecks);
}
