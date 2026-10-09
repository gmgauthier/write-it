/* SPDX-License-Identifier: Unlicense */

// Proves check.hpp's guard: a suite must run exactly the number of checks it
// names. One too few (an early return), one too many, or no count at all
// fails; exactly the count passes. Meson runs each mode as its own test.

#include "check.hpp"

#include <cstring>

namespace {

// Read at run time so the early return is not dead code to the compiler.
volatile bool stop_early = true;

int early()
{
  CHECK(true);
  if (stop_early)
    return suite_test::done("guard-early", 3);
  CHECK(true);
  CHECK(true);
  return suite_test::done("guard-early", 3);
}

int too_many()
{
  CHECK(true);
  CHECK(true);
  CHECK(true);
  CHECK(true);
  return suite_test::done("guard-too-many", 3);
}

int no_count()
{
  CHECK(true);
  return suite_test::done("guard-no-count", 0);
}

int exact()
{
  CHECK(true);
  CHECK(true);
  CHECK(true);
  return suite_test::done("guard-exact", 3);
}

}  // namespace

int main(int argc, char** argv)
{
  const char* mode = argc > 1 ? argv[1] : "";
  if (std::strcmp(mode, "early") == 0)
    return early();
  if (std::strcmp(mode, "too-many") == 0)
    return too_many();
  if (std::strcmp(mode, "no-count") == 0)
    return no_count();
  if (std::strcmp(mode, "exact") == 0)
    return exact();
  return EXIT_FAILURE;
}
