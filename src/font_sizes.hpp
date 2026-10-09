/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace writeit {

// The format toolbar's size box. It lists Word 97's presets; text in another
// size (a 20 pt heading, or a size from a file) shows its own size, listed in
// order among the presets, rather than whichever preset was showing before.
//
// Sizes are points in half-point steps, as Word 97 and RTF's \fsN keep them:
// 10.5 pt is \fs21. A double holds every half point exactly.

constexpr int kMinFontSize = 1;
// Word's largest.
constexpr int kMaxFontSize = 1638;

const std::vector<int>& preset_sizes();
bool preset_size(double size);

// A size Word can hold: 1 to 1638 pt in half points.
bool valid_size(double size);

// RTF's \fsN, in half points, as a size. Junk is clamped to Word's range,
// \fs2 (1 pt) to \fs3276 (1638 pt).
double size_from_half_points(int half_points);
// A size as RTF's \fsN, rounded to the nearest half point and clamped to the
// same range. Anything that is not a number is 11 pt, \fs22.
int half_points_of(double size);

// What the box lists for text of `actual` points: the presets, plus `actual`
// in order when it is a size (valid_size()) and not a preset.
std::vector<double> size_choices(double actual);

// A size as the box shows it, Word 97's way: "11", "10.5".
std::string size_text(double size);

// The size an entry in the box stands for: a whole number from 1 to 1638,
// or a half point between them ("10.5", also "10.0" and "10.50"), else 0.
// Word refuses other fractions (10.3) rather than rounding them, and so
// does this.
double parse_size(const std::string& text);

}  // namespace writeit
