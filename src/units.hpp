/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>

namespace writeit {

// Tools > Options... measurement units. Only the Paragraph dialog uses them.
// The document always holds twips, so the unit never changes a file.
enum class Units { Inches, Centimetres };

// "in" or "cm". Anything else, including nothing, is inches.
Units units_from_text(const std::string& text);
const char* units_text(Units units);

// The largest measure the dialog can produce, either side of zero: 22 inches,
// the model's indent ceiling.
constexpr int kMaxMeasureTwips = 31680;

double twips_to_units(int twips, Units units);
// Clamped to -kMaxMeasureTwips..kMaxMeasureTwips before it is converted, so
// no input overflows. NaN is 0.
int units_to_twips(double value, Units units);

// Word 97 steps: 0.1" and 0.25 cm, both shown to two decimals.
double units_step(Units units);
int units_digits(Units units);
double units_round(double value, Units units);

// 0.5" and 1.27 cm. Trailing zeros are dropped.
std::string format_measure(double value, Units units);
// Accepts a bare number in the chosen unit, or one with ", in, or cm. The
// other unit typed explicitly is converted. False when it is not a measure.
bool parse_measure(const std::string& text, Units units, double& value);

// The twips to store for a dialog field. An untouched field keeps the file's
// twips exactly, because the value on screen is rounded.
int keep_twips(int original, double shown, double now, Units units);

// The twips a Hanging By of `now` stores, given `twips` as worked out for it
// (keep_twips). A
// By equal to Left on screen takes Left's twips, so a rounded Left can hang
// by its own displayed amount without landing a twip past the margin.
int hang_twips(double now, int twips, int left_twips, double left_now, Units units);

}  // namespace writeit
