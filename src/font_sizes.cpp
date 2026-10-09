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

namespace {

enum class Entry { Size, NotNumber, OutOfRange, BadFraction };

// Reads an entry Word 97's way: spaces around it are trimmed, then an
// optional minus, digits, and an optional point with digits after it.
// Leading zeros count for nothing, so "00012" is 12 as "012" is.
Entry read_entry(const std::string& text, double& size)
{
  size = 0;
  const char* const kSpace = " \t";
  const size_t first = text.find_first_not_of(kSpace);
  if (first == std::string::npos)
    return Entry::NotNumber;
  const std::string entry = text.substr(first, text.find_last_not_of(kSpace) - first + 1);
  size_t at = 0;
  const bool negative = entry[0] == '-';
  if (negative)
    ++at;
  const size_t dot = entry.find('.', at);
  const std::string whole = entry.substr(at, dot == std::string::npos ? dot : dot - at);
  const std::string fraction = dot == std::string::npos ? "" : entry.substr(dot + 1);
  auto digits = [](const std::string& part) {
    return !part.empty() && part.find_first_not_of("0123456789") == std::string::npos;
  };
  if (!digits(whole) || (dot != std::string::npos && !digits(fraction)))
    return Entry::NotNumber;
  const size_t significant = whole.find_first_not_of('0');
  const std::string value = significant == std::string::npos ? "" : whole.substr(significant);
  const bool zero_fraction = fraction.find_first_not_of('0') == std::string::npos;
  // Past four digits it is past 1638, and never parsed into an overflow.
  if (value.size() > 4 || (negative && !(value.empty() && zero_fraction)))
    return Entry::OutOfRange;
  double number = value.empty() ? 0 : std::stoi(value);
  const bool half = !fraction.empty() && fraction[0] == '5' &&
                    fraction.find_first_not_of('0', 1) == std::string::npos;
  if (half)
    number += 0.5;
  else if (!zero_fraction)
    // Not a half point; how far from one does not matter here, only the
    // range: 0.3 and 1638.3 are out of it, 10.3 is in it.
    number += 0.25;
  if (number < kMinFontSize || number > kMaxFontSize)
    return Entry::OutOfRange;
  if (!half && !zero_fraction)
    return Entry::BadFraction;
  size = number;
  return Entry::Size;
}

}  // namespace

double parse_size(const std::string& text)
{
  double size = 0;
  return read_entry(text, size) == Entry::Size ? size : 0;
}

std::string size_refusal(const std::string& text)
{
  double size = 0;
  switch (read_entry(text, size)) {
    case Entry::Size:
      return "";
    case Entry::NotNumber:
      return "This is not a valid number.";
    case Entry::OutOfRange:
      return "The number must be between " + std::to_string(kMinFontSize) + " and " +
             std::to_string(kMaxFontSize) + ".";
    case Entry::BadFraction:
      break;
  }
  return "Font sizes must be whole numbers or end in .5.";
}

}  // namespace writeit
