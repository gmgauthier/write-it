/* SPDX-License-Identifier: Unlicense */

// assert_single_format_tag(buffer): one CHECK that no character in the
// buffer carries two character format tags (fmt*) or two paragraph format
// tags (para*). On failure it names the first such character and its tags.
// The rule itself is writeit::first_doubled_format_tag() in
// src/format_tags.hpp, which suites with their own failure path (the undo
// fuzz) call directly.

#pragma once

#include "check.hpp"
#include "format_tags.hpp"

#include <iostream>
#include <string>

#define assert_single_format_tag(buffer)                                                    \
  do {                                                                                     \
    std::string why_;                                                                      \
    const int at_ = ::writeit::first_doubled_format_tag((buffer), &why_);                  \
    if (at_ >= 0)                                                                          \
      std::cerr << "  doubled format tag: " << why_ << "\n";                               \
    ::suite_test::check(at_ < 0, "assert_single_format_tag(" #buffer ")", __FILE__, __LINE__); \
  } while (0)
