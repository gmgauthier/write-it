/* SPDX-License-Identifier: Unlicense */

#include "font_sizes.hpp"

#include <algorithm>

namespace writeit {

const std::vector<int>& preset_sizes()
{
  static const std::vector<int> kPresets = {8, 9, 10, 11, 12, 14, 16, 18, 24, 36};
  return kPresets;
}

bool preset_size(int size)
{
  const std::vector<int>& presets = preset_sizes();
  return std::find(presets.begin(), presets.end(), size) != presets.end();
}

std::vector<int> size_choices(int actual)
{
  std::vector<int> choices = preset_sizes();
  if (actual < kMinFontSize || actual > kMaxFontSize || preset_size(actual))
    return choices;
  choices.insert(std::upper_bound(choices.begin(), choices.end(), actual), actual);
  return choices;
}

int parse_size(const std::string& text)
{
  // Digits only, and no more than the largest size has, so nothing overflows.
  if (text.empty() || text.size() > 4)
    return 0;
  int value = 0;
  for (char c : text) {
    if (c < '0' || c > '9')
      return 0;
    value = value * 10 + (c - '0');
  }
  return value >= kMinFontSize && value <= kMaxFontSize ? value : 0;
}

}  // namespace writeit
