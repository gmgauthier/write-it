/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace writeit {

// The format toolbar's size box. It lists Word 97's presets; text in another
// size (a 20 pt heading, or a size from a file) shows its own size, listed in
// order among the presets, rather than whichever preset was showing before.

constexpr int kMinFontSize = 1;
// Word's largest.
constexpr int kMaxFontSize = 1638;

const std::vector<int>& preset_sizes();
bool preset_size(int size);

// What the box lists for text of `actual` points: the presets, plus `actual`
// in order when it is a size (1 to 1638) and not a preset.
std::vector<int> size_choices(int actual);

// The size an entry in the box stands for: a whole number from 1 to 1638,
// else 0.
int parse_size(const std::string& text);

}  // namespace writeit
