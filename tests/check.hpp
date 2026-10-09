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

// `at_least` is how many checks the suite must have run; fewer means part
// of it was skipped, and that is a failure too.
inline int done(const char* name, int at_least = 0)
{
  if (checks < at_least) {
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

}  // namespace suite_test

#define CHECK(expr) ::suite_test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
