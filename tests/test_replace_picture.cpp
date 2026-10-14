/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "main_window.hpp"

namespace {

void picture_is_not_the_needle()
{
  Glib::ustring hello_pic("hello");
  hello_pic.push_back(static_cast<gunichar>(0xFFFC));
  CHECK(!writeit::selection_matches(hello_pic, "hello", true));
  CHECK(!writeit::selection_matches(hello_pic, "hello", false));
  Glib::ustring pic;
  pic.push_back(static_cast<gunichar>(0xFFFC));
  CHECK(!writeit::selection_matches(pic, "hello", true));
  CHECK(writeit::selection_matches("hello", "hello", true));
  CHECK(writeit::selection_matches("Hello", "hello", false));
  CHECK(!writeit::selection_matches("Hello", "hello", true));
  CHECK(writeit::selection_matches("HELLO", "hello", false));
  CHECK(!writeit::selection_matches("hello", "", true));
}

}  // namespace

constexpr int kChecks = 8;

int main()
{
  picture_is_not_the_needle();
  return suite_test::done("replace-picture", kChecks);
}
