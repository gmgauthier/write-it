/* SPDX-License-Identifier: Unlicense */

#include "font_sizes.hpp"

#include <algorithm>
#include <cmath>

namespace writeit {

const std::vector<int>& preset_sizes()
{
  static const std::vector<int> kPresets = {8, 9, 10, 11, 12, 14, 16, 18, 24, 36};
  return kPresets;
}

bool preset_size(double size)
{
  const std::vector<int>& presets = preset_sizes();
  return std::find(presets.begin(), presets.end(), size) != presets.end();
}

bool valid_size(double size)
{
  // NaN fails both comparisons.
  return size >= kMinFontSize && size <= kMaxFontSize && std::floor(size * 2) == size * 2;
}

double size_from_half_points(int half_points)
{
  return std::max(2 * kMinFontSize, std::min(2 * kMaxFontSize, half_points)) / 2.0;
}

int half_points_of(double size)
{
  if (!(size == size))
    return 22;
  const double half = std::round(size * 2);
  if (half <= 2 * kMinFontSize)
    return 2 * kMinFontSize;
  if (half >= 2 * kMaxFontSize)
    return 2 * kMaxFontSize;
  return static_cast<int>(half);
}

std::vector<double> size_choices(double actual)
{
  std::vector<double> choices(preset_sizes().begin(), preset_sizes().end());
  if (!valid_size(actual) || preset_size(actual))
    return choices;
  choices.insert(std::upper_bound(choices.begin(), choices.end(), actual), actual);
  return choices;
}

std::string size_text(double size)
{
  const int half = half_points_of(size);
  std::string text = std::to_string(half / 2);
  if (half % 2 != 0)
    text += ".5";
  return text;
}

double parse_size(const std::string& text)
{
  // Whole points: digits only, and no more than the largest size has, so
  // nothing overflows. Then at most a half: ".5" or ".50", or ".0", ".00".
  const size_t dot = text.find('.');
  const std::string whole = text.substr(0, dot);
  if (whole.empty() || whole.size() > 4)
    return 0;
  int value = 0;
  for (char c : whole) {
    if (c < '0' || c > '9')
      return 0;
    value = value * 10 + (c - '0');
  }
  double size = value;
  if (dot != std::string::npos) {
    const std::string fraction = text.substr(dot + 1);
    if (fraction == "5" || fraction == "50")
      size += 0.5;
    else if (fraction != "0" && fraction != "00")
      return 0;
  }
  return valid_size(size) ? size : 0;
}

std::string size_refusal(const std::string& /*text*/)
{
  return "";
}

}  // namespace writeit
