/* SPDX-License-Identifier: Unlicense */

#include "para_check.hpp"

#include <algorithm>
#include <cmath>

namespace writeit {
namespace {

constexpr const char* kHangPastMargin =
    "The hanging indent is larger than the left indent. The first line cannot start to the left "
    "of the margin.";

std::string range_text(Units units)
{
  return "The measurement must be between " + format_measure(0, units) + " and " +
         format_measure(max_measure(units), units) + ".";
}

// The text area's width in the unit, as the warnings give it: 6.98" or 17.73 cm.
std::string width_text(int text_width, Units units)
{
  return format_measure(twips_to_units(text_width, units), units);
}

}  // namespace

double max_measure(Units units)
{
  return units_round(twips_to_units(kMaxIndent, units), units);
}

MeasureCheck check_measure(const std::string& text, Units units, double& value)
{
  double parsed = 0;
  if (!parse_measure(text, units, parsed))
    return MeasureCheck::NotMeasure;
  // Judged as the field shows it, to two decimals: 22.004" is 22".
  const double shown = units_round(parsed, units);
  const double max = max_measure(units);
  if (!(shown >= 0 && shown <= max))
    return MeasureCheck::OutOfRange;
  // In range as shown, so never stored past either end: -0.004 is 0.
  value = std::max(0.0, std::min(max, parsed));
  return MeasureCheck::Ok;
}

std::string measure_message(MeasureCheck check, Units units)
{
  switch (check) {
    case MeasureCheck::OutOfRange:
      return range_text(units);
    case MeasureCheck::NotMeasure:
      return "This is not a valid measurement. " + range_text(units);
    case MeasureCheck::Ok:
      break;
  }
  return "";
}

ParaCheck check_paragraph(const ParaFields& fields, int text_width)
{
  const Units units = fields.units;
  ParaCheck result;
  auto refuse = [&result](ParaField field, std::string message) {
    result.field = field;
    result.message = std::move(message);
    return result;
  };
  double left = 0;
  double right = 0;
  double by = 0;
  MeasureCheck check = check_measure(fields.left, units, left);
  if (check != MeasureCheck::Ok)
    return refuse(ParaField::Left, measure_message(check, units));
  check = check_measure(fields.right, units, right);
  if (check != MeasureCheck::Ok)
    return refuse(ParaField::Right, measure_message(check, units));
  // By is greyed out, and not read, while Special is (none).
  if (fields.special != 0) {
    check = check_measure(fields.by, units, by);
    if (check != MeasureCheck::Ok)
      return refuse(ParaField::By, measure_message(check, units));
  }

  // An untouched field keeps the file's twips, so OK on an unchanged dialog
  // changes nothing even where the value on screen is rounded.
  const Indents& current = fields.current;
  const int magnitude = current.first < 0 ? -current.first : current.first;
  Indents chosen;
  chosen.left = keep_twips(current.left, fields.left_shown, left, units);
  chosen.right = keep_twips(current.right, fields.right_shown, right, units);
  if (fields.special != 0) {
    int amount = keep_twips(magnitude, fields.by_shown, by, units);
    if (fields.special == 2)
      amount = hang_twips(by, amount, chosen.left, left, units);
    chosen.first = fields.special == 1 ? amount : -amount;
  }
  const bool left_edited = left != fields.left_shown;
  const bool right_edited = right != fields.right_shown;
  const bool by_edited = fields.special != fields.special_was || by != fields.by_shown;

  // Word 97 will not let the first line start left of the left margin. The
  // By field is at fault unless only Left changed.
  if (!indents_fit(chosen))
    return refuse(by_edited || !left_edited ? ParaField::By : ParaField::Left, kHangPastMargin);

  // Nor will it take indents that leave too little room for the text: at
  // least kMinTextTwips of the text area, in twips and as the dialog shows
  // it, so a total that rounds to fit on screen still leaves real width. A
  // first-line indent starts its line further right; a hanging one gives it
  // more room.
  const int digits_scale = units_digits(units) == 2 ? 100 : 1;
  auto shown = [units, digits_scale](long twips) {
    return std::llround(twips_to_units(static_cast<int>(twips), units) * digits_scale);
  };
  const long long shown_room = shown(text_width) - shown(kMinTextTwips);
  auto too_wide = [&](long twips) {
    return twips + kMinTextTwips > text_width || shown(twips) > shown_room;
  };
  const std::string room = " leave too little room for text. They must leave at least " +
                           format_measure(twips_to_units(kMinTextTwips, units), units) +
                           " of the " + width_text(text_width, units) + " text area.";
  const long sides = static_cast<long>(chosen.left) + chosen.right;
  const long first_line = sides + std::max(0, chosen.first);
  if (too_wide(sides)) {
    // A side too wide on its own is the one to fix; otherwise Right is at
    // fault unless only Left changed.
    ParaField field = left_edited && !right_edited ? ParaField::Left : ParaField::Right;
    if (too_wide(chosen.left))
      field = ParaField::Left;
    else if (too_wide(chosen.right))
      field = ParaField::Right;
    return refuse(field, "The left and right indents" + room);
  }
  if (too_wide(first_line)) {
    // By is at fault when it or Special changed, else the side that did.
    ParaField field = ParaField::By;
    if (!by_edited && right_edited && !left_edited)
      field = ParaField::Right;
    else if (!by_edited && left_edited)
      field = ParaField::Left;
    return refuse(field, "The left, first-line and right indents" + room);
  }
  result.indents = chosen;
  return result;
}

}  // namespace writeit
