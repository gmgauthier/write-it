/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>

#include "document.hpp"
#include "units.hpp"
#include "view.hpp"

namespace writeit {

// Format > Paragraph's checks, apart from GTK so the tests can run them. As
// in Word 97, OK on a choice the document cannot take shows a message and
// goes back to the dialog with the field to fix selected. Nothing is quietly
// reverted, clamped or shrunk.

// One indent field's text. Left, Right and By run from 0 through 22"
// (kMaxIndent), 55.88 cm, judged at the two decimals the field shows.
enum class MeasureCheck { Ok, NotMeasure, OutOfRange };

// The largest value a field holds in the unit: 22 or 55.88.
double max_measure(Units units);
// Ok sets `value` to the measure typed, in the unit; otherwise `value` is
// left alone.
MeasureCheck check_measure(const std::string& text, Units units, double& value);
// The warning for a field that is not Ok, naming the range in the unit:
//   The measurement must be between 0" and 22".
//   This is not a valid measurement. The measurement must be between 0" and 22".
// Empty for Ok.
std::string measure_message(MeasureCheck check, Units units);

// The least width the indents must leave for text is kMinTextTwips (document.hpp):
// a quarter inch, 0.64 cm, as Word 97 keeps some. At 50% that is still 8 px of wrap.

enum class ParaField { None, Left, Right, By };

// The dialog as OK finds it.
struct ParaFields {
  Units units = Units::Inches;
  // The paragraph's indents when the dialog opened.
  Indents current;
  // What each field holds now, as typed or as the dialog wrote it.
  std::string left;
  std::string right;
  std::string by;
  // Each field's value when the dialog opened, to tell an untouched field.
  double left_shown = 0;
  double right_shown = 0;
  double by_shown = 0;
  // Special: 0 (none), 1 First line, 2 Hanging; now and when it opened.
  int special = 0;
  int special_was = 0;
};

struct ParaCheck {
  // The field to select and fix, or None when OK may close the dialog.
  ParaField field = ParaField::None;
  // The warning to show; empty when field is None.
  std::string message;
  // The indents OK applies, when field is None.
  Indents indents;
};

// Format > Paragraph's spacing, apart from the indent checks above so those
// tests stay as they are. Before and after use Tools > Options. At least and
// Exactly are a point size, 0 pt through 1584 pt. Multiple is a number of
// lines, 0 through 132. Single, 1.5 and Double do not read At.
enum class SpacingField { None, Before, After, At };

struct SpacingFields {
  Units units = Units::Inches;
  Spacing current;
  std::string before;
  std::string after;
  std::string at;
  double before_shown = 0;
  double after_shown = 0;
  double at_shown = 0;
  LineRule rule = LineRule::Single;
};

struct SpacingCheck {
  SpacingField field = SpacingField::None;
  std::string message;
  Spacing spacing;
};

// Before, then After, then At when the rule uses it. An untouched field keeps
// its twips. Multiple of 1, 1.5 or 2 is stored as single, 1.5 or double, which
// is how \sl240, \sl360 and \sl480 with \slmult1 read back.
SpacingCheck check_spacing(const SpacingFields& fields);

// The fields first, Left, Right, then By (only when Special is on); then a
// hanging indent past the left margin (indents_fit); then indents that leave
// too little room for text: Left and Right, or the first line's start and
// Right, leaving less than kMinTextTwips of `text_width`, in twips or as the
// dialog shows them (6.73" in all is the most, 17.09 cm). A side too wide on
// its own is selected first. An untouched field keeps the file's twips
// (keep_twips), so OK on an unchanged dialog changes nothing.
ParaCheck check_paragraph(const ParaFields& fields, int text_width = kTextWidthTwips);

}  // namespace writeit
