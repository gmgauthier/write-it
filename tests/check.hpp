/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <cstdlib>
#include <iostream>

namespace suite_test {

inline int failures = 0;
// Checks run, so a suite can tell when it ran more or fewer than it should.
inline int checks = 0;

inline void check(bool ok, const char* expr, const char* file, int line)
{
  ++checks;
  if (!ok) {
    std::cerr << file << ":" << line << ": " << expr << "\n";
    ++failures;
  }
}

// Ends a suite. `expected` is exactly how many checks it runs. Every suite
// is deterministic, so any other number is a failure: fewer means part of it
// was skipped (an early return, a loop that ran short), more means a loop
// ran long or checks were added without updating the count. A count of 0 or
// less fails, and done() without one does not compile.
inline int done(const char* name, int expected)
{
  if (expected <= 0) {
    std::cerr << name << ": no check count given\n";
    ++failures;
  } else if (checks != expected) {
    std::cerr << name << ": " << checks << " checks ran, expected exactly " << expected << "\n";
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
