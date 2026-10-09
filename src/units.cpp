/* SPDX-License-Identifier: Unlicense */

#include "units.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace writeit {
namespace {

constexpr double kTwipsPerInch = 1440.0;
constexpr double kCmPerInch = 2.54;

double twips_per_unit(Units units)
{
  return units == Units::Centimetres ? kTwipsPerInch / kCmPerInch : kTwipsPerInch;
}

std::string trim_lower(const std::string& text)
{
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  std::string out = text.substr(begin, end - begin);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

Units units_from_text(const std::string& text)
{
  return trim_lower(text) == "cm" ? Units::Centimetres : Units::Inches;
}

const char* units_text(Units units)
{
  return units == Units::Centimetres ? "cm" : "in";
}

double twips_to_units(int twips, Units units)
{
  return static_cast<double>(twips) / twips_per_unit(units);
}

int units_to_twips(double value, Units units)
{
  return static_cast<int>(std::lround(value * twips_per_unit(units)));
}

double units_step(Units units)
{
  return units == Units::Centimetres ? 0.25 : 0.1;
}

int units_digits(Units /*units*/)
{
  return 2;
}

double units_round(double value, Units units)
{
  const double scale = std::pow(10.0, units_digits(units));
  return std::round(value * scale) / scale;
}

std::string format_measure(double value, Units units)
{
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", units_digits(units), units_round(value, units));
  std::string text = buf;
  if (text.find('.') != std::string::npos) {
    while (!text.empty() && text.back() == '0')
      text.pop_back();
    if (!text.empty() && text.back() == '.')
      text.pop_back();
  }
  if (text == "-0")
    text = "0";
  return text + (units == Units::Centimetres ? " cm" : "\"");
}

bool parse_measure(const std::string& raw, Units units, double& value)
{
  const std::string text = trim_lower(raw);
  if (text.empty())
    return false;
  // Parse the number by hand: strtod would also take the locale's comma,
  // hex, and exponents, none of which belong in this field.
  size_t i = 0;
  if (text[i] == '+' || text[i] == '-')
    ++i;
  const size_t digits_from = i;
  bool dot = false;
  size_t digits = 0;
  for (; i < text.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(text[i]))) {
      ++digits;
    } else if (text[i] == '.' && !dot) {
      dot = true;
    } else {
      break;
    }
  }
  if (digits == 0)
    return false;
  double number = 0;
  double scale = 0;
  for (size_t k = digits_from; k < i; ++k) {
    if (text[k] == '.') {
      scale = 1;
      continue;
    }
    number = number * 10 + (text[k] - '0');
    if (scale > 0)
      scale *= 10;
  }
  if (scale > 0)
    number /= scale;
  if (text[0] == '-')
    number = -number;
  std::string suffix = trim_lower(text.substr(i));
  Units given = units;
  if (suffix.empty()) {
    given = units;
  } else if (suffix == "\"" || suffix == "in" || suffix == "inch" || suffix == "inches") {
    given = Units::Inches;
  } else if (suffix == "cm") {
    given = Units::Centimetres;
  } else {
    return false;
  }
  if (given == units)
    value = number;
  else
    value = given == Units::Inches ? number * kCmPerInch : number / kCmPerInch;
  return true;
}

int keep_twips(int original, double shown, double now, Units units)
{
  const double half = 0.5 / std::pow(10.0, units_digits(units));
  if (std::fabs(now - shown) < half)
    return original;
  return units_to_twips(now, units);
}

}  // namespace writeit
