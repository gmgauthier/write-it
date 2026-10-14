/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "main_window.hpp"

namespace {

void ascii_still_matches()
{
  const writeit::FindHit exact = writeit::find_in_text("Hello world", "world", 0, true);
  CHECK(exact.index == 6);
  CHECK(exact.length == 5);
  const writeit::FindHit folded = writeit::find_in_text("Hello world", "WORLD", 0, false);
  CHECK(folded.index == 6);
  CHECK(folded.length == 5);
  const writeit::FindHit missed = writeit::find_in_text("Hello world", "WORLD", 0, true);
  CHECK(missed.index < 0);
  const writeit::FindHit wrapped = writeit::find_in_text("Hello world", "Hello", 6, false);
  CHECK(wrapped.index == 0);
  CHECK(wrapped.length == 5);
}

void fold_changes_length()
{
  const Glib::ustring street("Stra\u00dfe");
  CHECK(street.length() == 6);
  CHECK(street.casefold() == "strasse");
  const Glib::ustring line = "xx" + street + "yy";
  const writeit::FindHit lower = writeit::find_in_text(line, "strasse", 0, false);
  CHECK(lower.index == 2);
  CHECK(lower.length == 6);
  const writeit::FindHit upper = writeit::find_in_text(line, "STRASSE", 0, false);
  CHECK(upper.index == 2);
  CHECK(upper.length == 6);
  const writeit::FindHit sharp = writeit::find_in_text("a\u00df" "b", "ss", 0, false);
  CHECK(sharp.index == 1);
  CHECK(sharp.length == 1);
  const Glib::ustring dotted("a\u0130" "b");
  const writeit::FindHit big_i = writeit::find_in_text(dotted, "i", 0, false);
  CHECK(big_i.index == 1);
  CHECK(big_i.length == 1);
  const writeit::FindHit later = writeit::find_in_text(line, "strasse", 3, false);
  CHECK(later.index == 2);
}

}  // namespace

constexpr int kChecks = 18;

int main()
{
  ascii_still_matches();
  fold_changes_length();
  return suite_test::done("find-casefold", kChecks);
}
