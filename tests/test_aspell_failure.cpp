/* SPDX-License-Identifier: Unlicense */

// A dictionary name that cannot be opened. This does not spawn aspell.

#include "check.hpp"
#include "spelling.hpp"

void dead_dictionary()
{
  writeit::AspellDictionary dict("");
  CHECK(!dict.available());
  CHECK(!dict.contains("hello"));
  CHECK(!dict.contains(""));
  writeit::AspellDictionary spaced("bad name");
  CHECK(!spaced.available());
  CHECK(!spaced.contains("hello"));
}

constexpr int kChecks = 5;

int main()
{
  dead_dictionary();
  return suite_test::done("aspell-failure", kChecks);
}
