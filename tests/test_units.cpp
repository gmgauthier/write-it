/* SPDX-License-Identifier: Unlicense */

// Measurement units for the Paragraph dialog. The file always holds twips;
// the unit only changes what the dialog shows and accepts.

#include "check.hpp"
#include "units.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace {

using writeit::Units;

bool near(double a, double b)
{
  return std::fabs(a - b) < 1e-9;
}

void parsing_the_setting()
{
  CHECK(writeit::units_from_text("in") == Units::Inches);
  CHECK(writeit::units_from_text("cm") == Units::Centimetres);
  CHECK(writeit::units_from_text(" CM ") == Units::Centimetres);
  // Missing or invalid falls back to inches.
  CHECK(writeit::units_from_text("") == Units::Inches);
  CHECK(writeit::units_from_text("mm") == Units::Inches);
  CHECK(writeit::units_from_text("furlongs") == Units::Inches);
  CHECK(writeit::units_from_text("centimetres") == Units::Inches);
  CHECK(std::string(writeit::units_text(Units::Inches)) == "in");
  CHECK(std::string(writeit::units_text(Units::Centimetres)) == "cm");
  CHECK(writeit::units_from_text(writeit::units_text(Units::Centimetres)) == Units::Centimetres);
  CHECK(writeit::units_from_text(writeit::units_text(Units::Inches)) == Units::Inches);
}

void conversion()
{
  CHECK(near(writeit::twips_to_units(1440, Units::Inches), 1.0));
  CHECK(near(writeit::twips_to_units(720, Units::Inches), 0.5));
  CHECK(near(writeit::twips_to_units(1440, Units::Centimetres), 2.54));
  CHECK(near(writeit::twips_to_units(0, Units::Centimetres), 0.0));
  CHECK(writeit::units_to_twips(1.0, Units::Inches) == 1440);
  CHECK(writeit::units_to_twips(0.5, Units::Inches) == 720);
  CHECK(writeit::units_to_twips(0.1, Units::Inches) == 144);
  CHECK(writeit::units_to_twips(2.54, Units::Centimetres) == 1440);
  CHECK(writeit::units_to_twips(1.0, Units::Centimetres) == 567);
  CHECK(writeit::units_to_twips(1.27, Units::Centimetres) == 720);
  CHECK(writeit::units_to_twips(-0.5, Units::Inches) == -720);

  // Word 97 steps and precision.
  CHECK(near(writeit::units_step(Units::Inches), 0.1));
  CHECK(near(writeit::units_step(Units::Centimetres), 0.25));
  CHECK(writeit::units_digits(Units::Inches) == 2);
  CHECK(writeit::units_digits(Units::Centimetres) == 2);
  CHECK(near(writeit::units_round(0.98402, Units::Inches), 0.98));
  CHECK(near(writeit::units_round(2.4998, Units::Centimetres), 2.5));
}

void formatting()
{
  CHECK(writeit::format_measure(0.5, Units::Inches) == "0.5\"");
  CHECK(writeit::format_measure(0, Units::Inches) == "0\"");
  CHECK(writeit::format_measure(1.25, Units::Inches) == "1.25\"");
  CHECK(writeit::format_measure(1.0, Units::Inches) == "1\"");
  CHECK(writeit::format_measure(1.27, Units::Centimetres) == "1.27 cm");
  CHECK(writeit::format_measure(2.5, Units::Centimetres) == "2.5 cm");
  CHECK(writeit::format_measure(0, Units::Centimetres) == "0 cm");
  CHECK(writeit::format_measure(0.98402, Units::Inches) == "0.98\"");

  double value = -1;
  CHECK(writeit::parse_measure("0.5\"", Units::Inches, value) && near(value, 0.5));
  CHECK(writeit::parse_measure("0.5", Units::Inches, value) && near(value, 0.5));
  CHECK(writeit::parse_measure(" 0.5 \" ", Units::Inches, value) && near(value, 0.5));
  CHECK(writeit::parse_measure("0.5 in", Units::Inches, value) && near(value, 0.5));
  CHECK(writeit::parse_measure("1.27 cm", Units::Centimetres, value) && near(value, 1.27));
  CHECK(writeit::parse_measure("1.27", Units::Centimetres, value) && near(value, 1.27));
  CHECK(writeit::parse_measure(".75", Units::Inches, value) && near(value, 0.75));
  // The other unit typed explicitly is converted, as Word does.
  CHECK(writeit::parse_measure("2.54 cm", Units::Inches, value) && near(value, 1.0));
  CHECK(writeit::parse_measure("1\"", Units::Centimetres, value) && near(value, 2.54));
  CHECK(writeit::parse_measure("1 in", Units::Centimetres, value) && near(value, 2.54));
  CHECK(!writeit::parse_measure("", Units::Inches, value));
  CHECK(!writeit::parse_measure("abc", Units::Inches, value));
  CHECK(!writeit::parse_measure("1.5 furlongs", Units::Inches, value));
  CHECK(!writeit::parse_measure("1.2.3", Units::Inches, value));
}

void no_drift(Units units)
{
  // What the dialog shows survives its own text round trip, so focusing and
  // leaving a field does not move the value.
  for (int twips = 0; twips <= 31680; ++twips) {
    const double shown = writeit::units_round(writeit::twips_to_units(twips, units), units);
    double parsed = -1;
    const bool ok = writeit::parse_measure(writeit::format_measure(shown, units), units, parsed);
    if (!ok || !near(parsed, shown)) {
      CHECK(ok && near(parsed, shown));
      break;
    }
    // OK on an untouched field keeps the file's twips exactly, even where
    // the displayed value is rounded (1417 twips is 0.98" on screen).
    if (writeit::keep_twips(twips, shown, parsed, units) != twips) {
      CHECK(writeit::keep_twips(twips, shown, parsed, units) == twips);
      break;
    }
  }
  CHECK(writeit::keep_twips(1417, 0.98, 0.98, Units::Inches) == 1417);
  CHECK(writeit::keep_twips(1417, 2.5, 2.5, Units::Centimetres) == 1417);

  // A value the user types reopens as the same value: no drift on a second visit.
  for (int hundredths = 0; hundredths <= 2200; ++hundredths) {
    const double typed = hundredths / 100.0;
    const int twips = writeit::units_to_twips(typed, units);
    const double reopened = writeit::units_round(writeit::twips_to_units(twips, units), units);
    if (!near(reopened, typed)) {
      CHECK(near(reopened, typed));
      break;
    }
    // A changed field converts.
    CHECK(writeit::keep_twips(99999, typed + 0.01, typed, units) == twips);
  }

  // Switching units never changes the twips: the same twips read in the
  // other unit, untouched, come back identical.
  const Units other = units == Units::Inches ? Units::Centimetres : Units::Inches;
  for (int twips : {0, 1, 7, 567, 720, 1417, 1440, 31680}) {
    const double a = writeit::units_round(writeit::twips_to_units(twips, units), units);
    const double b = writeit::units_round(writeit::twips_to_units(twips, other), other);
    CHECK(writeit::keep_twips(twips, a, a, units) == twips);
    CHECK(writeit::keep_twips(twips, b, b, other) == twips);
  }
}

}  // namespace

// Bug Basher: units_to_twips(1e12) overflowed int. Whatever comes in, the
// result stays within the model's 22-inch range either side of zero (a
// first-line indent is negative when it hangs), and NaN is zero.
void conversion_is_clamped()
{
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(writeit::kMaxMeasureTwips == 31680);
  CHECK(writeit::units_to_twips(1e12, Units::Inches) == 31680);
  CHECK(writeit::units_to_twips(-1e12, Units::Inches) == -31680);
  CHECK(writeit::units_to_twips(1e12, Units::Centimetres) == 31680);
  CHECK(writeit::units_to_twips(-1e12, Units::Centimetres) == -31680);
  CHECK(writeit::units_to_twips(nan, Units::Inches) == 0);
  CHECK(writeit::units_to_twips(nan, Units::Centimetres) == 0);
  CHECK(writeit::units_to_twips(inf, Units::Inches) == 31680);
  CHECK(writeit::units_to_twips(-inf, Units::Inches) == -31680);
  CHECK(writeit::units_to_twips(inf, Units::Centimetres) == 31680);
  CHECK(writeit::units_to_twips(std::numeric_limits<double>::max(), Units::Inches) == 31680);
  CHECK(writeit::units_to_twips(2147483648.0, Units::Inches) == 31680);
  // The edges themselves.
  CHECK(writeit::units_to_twips(22.0, Units::Inches) == 31680);
  CHECK(writeit::units_to_twips(22.01, Units::Inches) == 31680);
  CHECK(writeit::units_to_twips(55.88, Units::Centimetres) == 31680);
  CHECK(writeit::units_to_twips(56.0, Units::Centimetres) == 31680);
  CHECK(writeit::units_to_twips(21.99, Units::Inches) == 31666);
  // keep_twips goes through the same clamp.
  CHECK(writeit::keep_twips(720, 0.5, 1e12, Units::Inches) == 31680);
  CHECK(writeit::keep_twips(720, 0.5, nan, Units::Inches) == 0);
}

// A hanging indent typed equal to Left, to the precision on screen, takes
// Left's twips exactly, so a Left of 1410 twips shown as 0.98" can hang by
// 0.98" without being one twip past the margin.
void hang_matches_left()
{
  // hang_twips(now, twips worked out for now, Left twips, Left on screen)
  CHECK(writeit::hang_twips(0.98, 1411, 1410, 0.98, Units::Inches) == 1410);
  CHECK(writeit::hang_twips(0.984, 1417, 1410, 0.98, Units::Inches) == 1410);
  CHECK(writeit::hang_twips(0.99, 1426, 1410, 0.98, Units::Inches) == 1426);
  CHECK(writeit::hang_twips(0.5, 720, 1410, 0.98, Units::Inches) == 720);
  CHECK(writeit::hang_twips(2.49, 1412, 1411, 2.49, Units::Centimetres) == 1411);
  CHECK(writeit::hang_twips(2.5, 1417, 1411, 2.49, Units::Centimetres) == 1417);
  // An untouched By keeps its own twips when it is not equal to Left.
  CHECK(writeit::hang_twips(0.5, 719, 1440, 1.0, Units::Inches) == 719);
}

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 4514;

int main()
{
  parsing_the_setting();
  conversion();
  formatting();
  no_drift(Units::Inches);
  no_drift(Units::Centimetres);
  conversion_is_clamped();
  hang_matches_left();
  return suite_test::done("units", kChecks);
}
