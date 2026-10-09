/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <cstdlib>
#include <iostream>

namespace suite_test {

inline int failures = 0;
// Checks run, so a suite can tell when it stopped early.
inline int checks = 0;

inline void check(bool ok, const char* expr, const char* file, int line)
{
  ++checks;
  if (!ok) {
    std::cerr << file << ":" << line << ": " << expr << "\n";
    ++failures;
  }
}

// Ends a suite. `at_least` is how many checks it must have run: fewer means
// part of it was skipped, by an early return or a loop that ran short, and
// that fails the suite. Every suite names one: a minimum of 0 or less fails,
// and done() without one does not compile.
inline int done(const char* name, int at_least)
{
  if (at_least <= 0) {
    std::cerr << name << ": no minimum check count given\n";
    ++failures;
  } else if (checks < at_least) {
    std::cerr << name << ": only " << checks << " checks ran, expected at least " << at_least
              << "\n";
    ++failures;
  }
  if (failures) {
    std::cerr << name << ": " << failures << " failed\n";
    return EXIT_FAILURE;
  }
  std::cout << name << ": ok, " << checks << " checks\n";
  return EXIT_SUCCESS;
}

// A suite must say how many checks it runs.
int done(const char* name) = delete;

}  // namespace suite_test

#define CHECK(expr) ::suite_test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
