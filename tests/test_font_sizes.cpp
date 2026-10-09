/* SPDX-License-Identifier: Unlicense */

// The format toolbar's size box shows the caret's real size. It used to show
// only its presets, so 20 pt text left "16" (or whatever was there) showing.
// Sizes go in half points, as in Word 97 and RTF's \fsN: 10.5 pt text shows
// "10.5", and 10.5 can be typed. Other fractions are refused, as Word does.

#include "check.hpp"
#include "font_sizes.hpp"

#include <string>
#include <vector>

namespace {

using writeit::parse_size;
using writeit::size_choices;
using writeit::size_text;

const std::vector<int> kPresets = {8, 9, 10, 11, 12, 14, 16, 18, 24, 36};
const std::vector<double> kChoices = {8, 9, 10, 11, 12, 14, 16, 18, 24, 36};

void presets()
{
  CHECK(writeit::preset_sizes() == kPresets);
  for (int size : kPresets)
    CHECK(writeit::preset_size(size));
  CHECK(!writeit::preset_size(20));
  CHECK(!writeit::preset_size(0));
  CHECK(!writeit::preset_size(-11));
  CHECK(!writeit::preset_size(10.5));
}

void choices()
{
  // A preset lists the presets.
  CHECK(size_choices(11) == kChoices);
  CHECK(size_choices(36) == kChoices);
  // Another size is listed in order among them.
  CHECK(size_choices(20) == (std::vector<double>{8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 36}));
  CHECK(size_choices(5) == (std::vector<double>{5, 8, 9, 10, 11, 12, 14, 16, 18, 24, 36}));
  CHECK(size_choices(72) == (std::vector<double>{8, 9, 10, 11, 12, 14, 16, 18, 24, 36, 72}));
  CHECK(size_choices(1) == (std::vector<double>{1, 8, 9, 10, 11, 12, 14, 16, 18, 24, 36}));
  CHECK(size_choices(1638) == (std::vector<double>{8, 9, 10, 11, 12, 14, 16, 18, 24, 36, 1638}));
  // Half points too: \fs21 is 10.5 pt, between 10 and 11.
  CHECK(size_choices(10.5) == (std::vector<double>{8, 9, 10, 10.5, 11, 12, 14, 16, 18, 24, 36}));
  CHECK(size_choices(8.5) == (std::vector<double>{8, 8.5, 9, 10, 11, 12, 14, 16, 18, 24, 36}));
  CHECK(size_choices(1637.5) ==
        (std::vector<double>{8, 9, 10, 11, 12, 14, 16, 18, 24, 36, 1637.5}));
  // Not a size: just the presets.
  CHECK(size_choices(0) == kChoices);
  CHECK(size_choices(-20) == kChoices);
  CHECK(size_choices(1639) == kChoices);
  CHECK(size_choices(2147483647) == kChoices);
  CHECK(size_choices(0.5) == kChoices);
  CHECK(size_choices(1638.5) == kChoices);
  // A size the box cannot hold (no file or entry makes one) is not listed.
  CHECK(size_choices(10.25) == kChoices);
}

// What the box shows for a size: whole points bare, half points with ".5".
void text()
{
  CHECK(size_text(11) == "11");
  CHECK(size_text(10.5) == "10.5");
  CHECK(size_text(8.5) == "8.5");
  CHECK(size_text(1) == "1");
  CHECK(size_text(1638) == "1638");
  CHECK(size_text(1637.5) == "1637.5");
  // Every half point from 1 to 1638 reads back as itself.
  bool all = true;
  for (int half = 2; half <= 2 * writeit::kMaxFontSize; ++half)
    all = all && parse_size(size_text(half / 2.0)) == half / 2.0;
  CHECK(all);
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
  // Half points, as Word 97's box takes them.
  CHECK(parse_size("10.5") == 10.5);
  CHECK(parse_size("8.5") == 8.5);
  CHECK(parse_size("1.5") == 1.5);
  CHECK(parse_size("1637.5") == 1637.5);
  CHECK(parse_size("10.0") == 10);
  CHECK(parse_size("10.50") == 10.5);
  // Word refuses any other fraction rather than rounding it.
  CHECK(parse_size("10.3") == 0);
  CHECK(parse_size("10.25") == 0);
  CHECK(parse_size("10.55") == 0);
  CHECK(parse_size("0.5") == 0);
  CHECK(parse_size("1638.5") == 0);
  CHECK(parse_size(".5") == 0);
  CHECK(parse_size("10.") == 0);
  CHECK(parse_size("10.5.5") == 0);
  CHECK(parse_size("10,5") == 0);
  CHECK(parse_size("1e1") == 0);
  CHECK(parse_size("10.5 ") == 0);
  CHECK(parse_size("99999999999999999999.5") == 0);
}

}  // namespace

int main()
{
  presets();
  choices();
  text();
  parsing();
  return suite_test::done("font-sizes", 68);
}
