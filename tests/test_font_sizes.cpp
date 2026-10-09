/* SPDX-License-Identifier: Unlicense */

// The format toolbar's size box shows the caret's real size. It used to show
// only its presets, so 20 pt text left "16" (or whatever was there) showing.

#include "check.hpp"
#include "font_sizes.hpp"

#include <vector>

namespace {

using writeit::parse_size;
using writeit::size_choices;

const std::vector<int> kPresets = {8, 9, 10, 11, 12, 14, 16, 18, 24, 36};

void presets()
{
  CHECK(writeit::preset_sizes() == kPresets);
  for (int size : kPresets)
    CHECK(writeit::preset_size(size));
  CHECK(!writeit::preset_size(20));
  CHECK(!writeit::preset_size(0));
  CHECK(!writeit::preset_size(-11));
}

void choices()
{
  // A preset lists the presets.
  CHECK(size_choices(11) == kPresets);
  CHECK(size_choices(36) == kPresets);
  // Another size is listed in order among them.
  CHECK(size_choices(20) == (std::vector<int>{8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 36}));
  CHECK(size_choices(5) == (std::vector<int>{5, 8, 9, 10, 11, 12, 14, 16, 18, 24, 36}));
  CHECK(size_choices(72) == (std::vector<int>{8, 9, 10, 11, 12, 14, 16, 18, 24, 36, 72}));
  CHECK(size_choices(1) == (std::vector<int>{1, 8, 9, 10, 11, 12, 14, 16, 18, 24, 36}));
  CHECK(size_choices(1638) == (std::vector<int>{8, 9, 10, 11, 12, 14, 16, 18, 24, 36, 1638}));
  // Not a size: just the presets.
  CHECK(size_choices(0) == kPresets);
  CHECK(size_choices(-20) == kPresets);
  CHECK(size_choices(1639) == kPresets);
  CHECK(size_choices(2147483647) == kPresets);
}

void parsing()
{
  CHECK(parse_size("11") == 11);
  CHECK(parse_size("20") == 20);
  CHECK(parse_size("1") == 1);
  CHECK(parse_size("1638") == 1638);
  CHECK(parse_size("0") == 0);
  CHECK(parse_size("1639") == 0);
  CHECK(parse_size("") == 0);
  CHECK(parse_size("-5") == 0);
  CHECK(parse_size("12pt") == 0);
  CHECK(parse_size(" 12") == 0);
  CHECK(parse_size("99999999999999999999") == 0);
}

}  // namespace

int main()
{
  presets();
  choices();
  parsing();
  return suite_test::done("font-sizes", 30);
}
