/* SPDX-License-Identifier: Unlicense */

// Proves check.hpp's guard: a suite that returns before its checks have run,
// or that gives no minimum, fails. Meson runs this three ways; the first two
// are expected to fail (should_fail), the third to pass.

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

int no_minimum()
{
  CHECK(true);
  return suite_test::done("guard-no-minimum", 0);
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
  if (std::strcmp(mode, "no-minimum") == 0)
    return no_minimum();
  if (std::strcmp(mode, "exact") == 0)
    return exact();
  return EXIT_FAILURE;
}
