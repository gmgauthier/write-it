/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <cstdlib>
#include <iostream>

namespace suite_test {

inline int failures = 0;

inline void check(bool ok, const char* expr, const char* file, int line)
{
  if (!ok) {
    std::cerr << file << ":" << line << ": " << expr << "\n";
    ++failures;
  }
}

inline int done(const char* name)
{
  if (failures) {
    std::cerr << name << ": " << failures << " failed\n";
    return EXIT_FAILURE;
  }
  std::cout << name << ": ok\n";
  return EXIT_SUCCESS;
}

}  // namespace suite_test

#define CHECK(expr) ::suite_test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
