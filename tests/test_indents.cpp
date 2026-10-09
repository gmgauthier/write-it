/* SPDX-License-Identifier: Unlicense */

// M2 paragraph indents: the model, RTF write and read (\li \ri \fi), and the
// edge cases a hand-written or Word-written file can carry.

#include "check.hpp"
#include "document.hpp"
#include "para_check.hpp"
#include "units.hpp"
#include "view.hpp"

#include <string>

namespace {

writeit::Paragraph para(const char* text, int left, int right, int first)
{
  writeit::Paragraph paragraph;
  paragraph.indents.left = left;
  paragraph.indents.right = right;
  paragraph.indents.first = first;
  if (text[0] != '\0') {
    writeit::Run run;
    run.text = text;
    paragraph.runs.push_back(run);
  }
  return paragraph;
}

bool contains(const std::string& haystack, const std::string& needle)
{
  return haystack.find(needle) != std::string::npos;
}

writeit::Document import(const std::string& rtf)
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(rtf, doc));
  return doc;
}

const char* kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}";

void model()
{
  // A new paragraph has no indent.
  const writeit::Paragraph blank;
  CHECK(blank.indents.left == 0);
  CHECK(blank.indents.right == 0);
  CHECK(blank.indents.first == 0);
  CHECK(blank.indents == writeit::Indents{});

  // Indents are part of paragraph equality, so undo and the dirty title see them.
  CHECK(!(para("a", 720, 0, 0) == para("a", 0, 0, 0)));
  CHECK(!(para("a", 0, 720, 0) == para("a", 0, 0, 0)));
  CHECK(!(para("a", 0, 0, 720) == para("a", 0, 0, 0)));
  CHECK(para("a", 720, 360, -360) == para("a", 720, 360, -360));

  const writeit::Document fresh = writeit::blank_document("Sans", 11);
  CHECK(fresh.paragraphs[0].indents == writeit::Indents{});

  // Centimetres via the units module, twips in the file. 1 in = 2.54 cm = 1440 twips.
  CHECK(writeit::units_to_twips(2.54, writeit::Units::Centimetres) == 1440);
  CHECK(writeit::units_to_twips(1.27, writeit::Units::Centimetres) == 720);
  CHECK(writeit::units_to_twips(0, writeit::Units::Centimetres) == 0);
  CHECK(writeit::units_to_twips(-1.27, writeit::Units::Centimetres) == -720);
  CHECK(writeit::units_to_twips(1.0, writeit::Units::Centimetres) == 567);
  CHECK(writeit::twips_to_units(1440, writeit::Units::Centimetres) > 2.539 &&
        writeit::twips_to_units(1440, writeit::Units::Centimetres) < 2.541);
  CHECK(writeit::twips_to_units(0, writeit::Units::Centimetres) == 0.0);
  // Two decimals in the dialog survive the trip through twips.
  for (int hundredths = 0; hundredths <= 1500; ++hundredths) {
    const double cm = hundredths / 100.0;
    const double back = writeit::twips_to_units(
        writeit::units_to_twips(cm, writeit::Units::Centimetres), writeit::Units::Centimetres);
    CHECK(back > cm - 0.005 && back < cm + 0.005);
  }

  // Clamping. Left and right stay on the page; the first line cannot hang
  // past the left margin; nothing exceeds the RTF ceiling of 22 inches.
  using writeit::clamp_indents;
  using writeit::Indents;
  using writeit::kMaxIndent;
  CHECK(kMaxIndent == 31680);
  CHECK(clamp_indents(Indents{720, 360, -360}) == (Indents{720, 360, -360}));
  CHECK(clamp_indents(Indents{-720, 0, 0}) == (Indents{0, 0, 0}));
  CHECK(clamp_indents(Indents{0, -5, 0}) == (Indents{0, 0, 0}));
  CHECK(clamp_indents(Indents{720, 0, -1440}) == (Indents{720, 0, -720}));
  CHECK(clamp_indents(Indents{0, 0, -360}) == (Indents{0, 0, 0}));
  CHECK(clamp_indents(Indents{99999, 99999, 99999}) ==
        (Indents{kMaxIndent, kMaxIndent, kMaxIndent}));
  CHECK(clamp_indents(Indents{kMaxIndent, 0, -99999}) == (Indents{kMaxIndent, 0, -kMaxIndent}));
}

void rtf_write()
{
  writeit::Document doc;
  doc.paragraphs.push_back(para("Left", 720, 0, 0));
  doc.paragraphs.push_back(para("Right", 0, 1440, 0));
  doc.paragraphs.push_back(para("Hanging", 720, 0, -360));
  doc.paragraphs.push_back(para("Plain", 0, 0, 0));
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(contains(rtf, "\\pard\\li720"));
  CHECK(contains(rtf, "\\ri1440"));
  CHECK(contains(rtf, "\\li720\\fi-360"));
  // A paragraph with no indent writes no indent words, as M1 did.
  CHECK(contains(rtf, "\\pard\\f0"));
  CHECK(!contains(rtf, "\\li0"));
  CHECK(!contains(rtf, "\\ri0"));
  CHECK(!contains(rtf, "\\fi0"));
}

void rtf_round_trip()
{
  writeit::Document doc;
  doc.paragraphs.push_back(para("Indented left", 720, 0, 0));
  doc.paragraphs.push_back(para("Indented right", 0, 1134, 0));
  doc.paragraphs.push_back(para("First line", 0, 0, 567));
  doc.paragraphs.push_back(para("Hanging", 1440, 0, -720));
  doc.paragraphs.push_back(para("All three", 360, 720, 1080));
  doc.paragraphs.push_back(para("Back to none", 0, 0, 0));
  doc.paragraphs.push_back(para("", 720, 0, 0));  // an empty indented paragraph
  doc.paragraphs.push_back(para("Last", 2880, 2880, -2880));

  const writeit::Document loaded = import(writeit::rtf_export(doc));
  CHECK(loaded.paragraphs.size() == doc.paragraphs.size());
  CHECK(loaded == doc);
  // Saving the loaded file again writes the same bytes.
  CHECK(writeit::rtf_export(loaded) == writeit::rtf_export(doc));

  // Indents and character format and headings travel together.
  writeit::Document mixed;
  writeit::Paragraph heading = para("Title", 720, 0, 0);
  heading.heading = 1;
  writeit::Paragraph body = para("plain ", 1440, 360, -360);
  writeit::Run bold;
  bold.text = "bold";
  bold.bold = true;
  body.runs.push_back(bold);
  mixed.paragraphs.push_back(heading);
  mixed.paragraphs.push_back(body);
  CHECK(import(writeit::rtf_export(mixed)) == mixed);

  // The ceiling round-trips.
  writeit::Document widest;
  widest.paragraphs.push_back(
      para("Wide", writeit::kMaxIndent, writeit::kMaxIndent, -writeit::kMaxIndent));
  CHECK(import(writeit::rtf_export(widest)) == widest);
}

void rtf_read()
{
  // \pard resets the indents. Without it a paragraph keeps the last ones, as
  // RTF specifies.
  {
    const writeit::Document doc = import(std::string(kHead) +
                                         "\\pard\\li720\\fi-360 one\\par "
                                         "two\\par "
                                         "\\pard three\\par}");
    CHECK(doc.paragraphs.size() == 3);
    CHECK(doc.paragraphs[0].indents == (writeit::Indents{720, 0, -360}));
    CHECK(doc.paragraphs[1].indents == (writeit::Indents{720, 0, -360}));
    CHECK(doc.paragraphs[2].indents == writeit::Indents{});
  }
  // Indents are paragraph properties: the value in force at \par wins, even
  // when the control word comes after the text.
  {
    const writeit::Document doc = import(std::string(kHead) + "\\pard text\\ri500\\par}");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(doc.paragraphs[0].indents.right == 500);
  }
  // A group restores the indents it changed.
  {
    const writeit::Document doc =
        import(std::string(kHead) + "\\pard\\li100 {\\li900 in}\\par after\\par}");
    CHECK(doc.paragraphs.size() == 2);
    CHECK(doc.paragraphs[0].indents.left == 100);
    CHECK(doc.paragraphs[1].indents.left == 100);
  }
  // \plain is character format only; it does not touch the indents.
  {
    const writeit::Document doc = import(std::string(kHead) + "\\pard\\li720\\plain x\\par}");
    CHECK(doc.paragraphs[0].indents.left == 720);
  }
  // Word 2000 and later also write \lin and \rin. For left-to-right text they
  // are the same as \li and \ri.
  {
    const writeit::Document doc = import(std::string(kHead) +
                                         "\\pard\\plain \\ltrpar\\ql \\li0\\ri0\\lin1440\\rin567"
                                         "\\fi-720\\f0\\fs22 Word\\par}");
    CHECK(doc.paragraphs[0].indents == (writeit::Indents{1440, 567, -720}));
  }
  // Out-of-range values are clamped on the way in.
  {
    const writeit::Document doc = import(std::string(kHead) +
                                         "\\pard\\li-720\\ri-1 a\\par"
                                         "\\pard\\li360\\fi-9999 b\\par"
                                         "\\pard\\li999999\\ri40000\\fi99999 c\\par"
                                         "\\pard\\li99999999999999999999 d\\par}");
    CHECK(doc.paragraphs.size() == 4);
    CHECK(doc.paragraphs[0].indents == writeit::Indents{});
    CHECK(doc.paragraphs[1].indents == (writeit::Indents{360, 0, -360}));
    CHECK(doc.paragraphs[2].indents ==
          (writeit::Indents{writeit::kMaxIndent, writeit::kMaxIndent, writeit::kMaxIndent}));
    // A digit run too long for an int is still just "too big".
    CHECK(doc.paragraphs[3].indents == (writeit::Indents{writeit::kMaxIndent, 0, 0}));
    CHECK(doc.paragraphs[3].runs.size() == 1);
    CHECK(doc.paragraphs[3].runs[0].text == "d");
  }
  // A bare control word with no number is ignored rather than read as zero
  // or as a stray value.
  {
    const writeit::Document doc =
        import(std::string(kHead) + "\\pard\\li720\\li\\ri\\fi text\\par}");
    CHECK(doc.paragraphs[0].indents == (writeit::Indents{720, 0, 0}));
  }
  // Indents inside the stylesheet describe a style, not the paragraph.
  {
    const writeit::Document doc = import(std::string(kHead) +
                                         "{\\stylesheet{\\s1\\li2000 Indented;}}"
                                         "\\pard text\\par}");
    CHECK(doc.paragraphs[0].indents == writeit::Indents{});
  }
  // The final paragraph with no closing \par keeps its indents.
  {
    const writeit::Document doc =
        import(std::string(kHead) + "\\pard one\\par\\pard\\li720\\fi360 two}");
    CHECK(doc.paragraphs.size() == 2);
    CHECK(doc.paragraphs[1].indents == (writeit::Indents{720, 0, 360}));
  }
  // A truncated file with no closing brace keeps them too.
  {
    const writeit::Document doc = import(std::string(kHead) + "\\pard\\ri288 cut");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(doc.paragraphs[0].indents.right == 288);
  }
}

void markdown()
{
  // Markdown leaves indents out on purpose (DEVELOPMENT.md section 6).
  writeit::Document doc;
  doc.paragraphs.push_back(para("Indented", 1440, 720, -360));
  CHECK(writeit::markdown_export(doc) == "Indented");
  const writeit::Document back = writeit::markdown_import("Indented", "Sans", 11);
  CHECK(back.paragraphs[0].indents == writeit::Indents{});
  const writeit::Document plain = writeit::plain_import("  two leading spaces\n", "Sans", 11);
  CHECK(plain.paragraphs[0].indents == writeit::Indents{});
}

// Bug Basher: a hanging indent larger than Left used to shrink silently.
// Word 97 refuses it in the Paragraph dialog instead; indents_fit is that
// check. The model clamp stays as a safety net for RTF input.
writeit::Indents indents(int left, int right, int first)
{
  writeit::Indents value;
  value.left = left;
  value.right = right;
  value.first = first;
  return value;
}

void dialog_validation()
{
  using writeit::Units;
  using writeit::indents_fit;
  // Boundary: hanging exactly to the margin is fine, one twip past is not.
  CHECK(indents_fit(indents(720, 0, -720)));
  CHECK(!indents_fit(indents(720, 0, -721)));
  CHECK(indents_fit(indents(0, 0, 0)));
  CHECK(!indents_fit(indents(0, 0, -1)));
  // Left 0 with a positive first line is fine, and so is any first line.
  CHECK(indents_fit(indents(0, 0, 360)));
  CHECK(indents_fit(indents(0, 0, writeit::kMaxIndent)));
  CHECK(indents_fit(indents(1440, 720, 720)));
  CHECK(indents_fit(indents(0, writeit::kMaxIndent, 0)));
  // Both units, the way the dialog builds them.
  auto hanging = [](double left, double by, Units units) {
    const int left_twips = writeit::units_to_twips(left, units);
    const int by_twips = writeit::units_to_twips(by, units);
    return indents(left_twips, 0, -writeit::hang_twips(by, by_twips, left_twips, left, units));
  };
  CHECK(indents_fit(hanging(1.0, 1.0, Units::Inches)));
  CHECK(indents_fit(hanging(1.0, 0.99, Units::Inches)));
  CHECK(!indents_fit(hanging(1.0, 1.01, Units::Inches)));
  CHECK(indents_fit(hanging(2.54, 2.54, Units::Centimetres)));
  CHECK(indents_fit(hanging(2.49, 2.49, Units::Centimetres)));
  CHECK(!indents_fit(hanging(2.54, 2.55, Units::Centimetres)));
  CHECK(!indents_fit(hanging(0.0, 0.5, Units::Inches)));
  CHECK(!indents_fit(hanging(0.0, 1.27, Units::Centimetres)));
  // The model clamp is unchanged: RTF can still say anything.
  CHECK(writeit::clamp_indents(indents(720, 0, -1440)) == indents(720, 0, -720));
}

// Bug Basher's live pass: typing 99, 30 or 1e3 into an indent field snapped
// back to the old value with no word. Word 97 says what the range is and
// keeps the field to fix; check_measure is that check.
void field_ranges()
{
  using writeit::check_measure;
  using writeit::MeasureCheck;
  using writeit::measure_message;
  using writeit::Units;
  const Units in = Units::Inches;
  const Units cm = Units::Centimetres;
  CHECK(writeit::max_measure(in) == 22.0);
  CHECK(writeit::max_measure(cm) == 55.88);
  double value = -1;
  CHECK(check_measure("0", in, value) == MeasureCheck::Ok && value == 0.0);
  CHECK(check_measure("22", in, value) == MeasureCheck::Ok && value == 22.0);
  CHECK(check_measure("22\"", in, value) == MeasureCheck::Ok && value == 22.0);
  CHECK(check_measure("21.99 in", in, value) == MeasureCheck::Ok && value == 21.99);
  CHECK(check_measure("55.88 cm", in, value) == MeasureCheck::Ok && value == 22.0);
  CHECK(check_measure("55.88", cm, value) == MeasureCheck::Ok && value == 55.88);
  CHECK(check_measure("22\"", cm, value) == MeasureCheck::Ok && value > 55.879 && value <= 55.88);
  CHECK(check_measure("1.27 cm", cm, value) == MeasureCheck::Ok && value == 1.27);
  // Rounded to the field's two decimals, 22.004 is 22": in range, and it
  // stores 22", never past the ceiling.
  CHECK(check_measure("22.004", in, value) == MeasureCheck::Ok && value == 22.0);
  CHECK(check_measure("-0", in, value) == MeasureCheck::Ok && value == 0.0);
  CHECK(check_measure("-0.004", in, value) == MeasureCheck::Ok && value == 0.0);

  // Out of range, either side, in either unit. The value is left alone.
  for (const char* text : {"99", "30", "22.01", "23", "-1", "-0.01", "22.01\"", "56 cm",
                           "1000000000000000000000000000000000000000"}) {
    value = -1;
    CHECK(check_measure(text, in, value) == MeasureCheck::OutOfRange && value == -1);
  }
  for (const char* text : {"99", "30 in", "55.89", "56", "141", "-0.01", "22.01\"", "-1 cm"}) {
    value = -1;
    CHECK(check_measure(text, cm, value) == MeasureCheck::OutOfRange && value == -1);
  }
  // Not a measure at all: 1e3 included, since the field takes no exponents.
  for (const char* text : {"1e3", "abc", "", "   ", "1,5", "0x10", "1 mm", "--1", ".", "\""}) {
    value = -1;
    CHECK(check_measure(text, in, value) == MeasureCheck::NotMeasure && value == -1);
    CHECK(check_measure(text, cm, value) == MeasureCheck::NotMeasure && value == -1);
  }
  // Every hundredth from 0 to 23" and 0 to 60 cm: in range exactly up to the
  // ceiling.
  for (int hundredths = 0; hundredths <= 2300; ++hundredths) {
    const std::string text = std::to_string(hundredths / 100) + "." +
                             std::to_string(hundredths / 10 % 10) + std::to_string(hundredths % 10);
    CHECK((check_measure(text, in, value) == MeasureCheck::Ok) == (hundredths <= 2200));
  }
  for (int hundredths = 0; hundredths <= 6000; ++hundredths) {
    const std::string text = std::to_string(hundredths / 100) + "." +
                             std::to_string(hundredths / 10 % 10) + std::to_string(hundredths % 10);
    CHECK((check_measure(text, cm, value) == MeasureCheck::Ok) == (hundredths <= 5588));
  }

  // The messages name the range in the unit Tools > Options... chose.
  CHECK(measure_message(MeasureCheck::OutOfRange, in) ==
        "The measurement must be between 0\" and 22\".");
  CHECK(measure_message(MeasureCheck::OutOfRange, cm) ==
        "The measurement must be between 0 cm and 55.88 cm.");
  CHECK(measure_message(MeasureCheck::NotMeasure, in) ==
        "This is not a valid measurement. The measurement must be between 0\" and 22\".");
  CHECK(measure_message(MeasureCheck::NotMeasure, cm) ==
        "This is not a valid measurement. The measurement must be between 0 cm and 55.88 cm.");
  CHECK(measure_message(MeasureCheck::Ok, in).empty());
}

// The dialog's fields as OK finds them, opened on `current` and showing what
// the dialog shows for it, then edited to `left`, `right`, `by` and `special`.
writeit::ParaFields fields(writeit::Indents current, writeit::Units units, const char* left,
                           const char* right, const char* by, int special)
{
  auto shown = [units](int twips) {
    return writeit::units_round(writeit::twips_to_units(twips, units), units);
  };
  writeit::ParaFields f;
  f.units = units;
  f.current = current;
  f.left = left;
  f.right = right;
  f.by = by;
  f.left_shown = shown(current.left);
  f.right_shown = shown(current.right);
  f.by_shown = shown(current.first < 0 ? -current.first : current.first);
  f.special = special;
  f.special_was = current.first > 0 ? 1 : current.first < 0 ? 2 : 0;
  return f;
}

// Bug Basher's live pass: Left 22" and Right 22" was accepted, and the
// paragraph left the page. Word 97 refuses indents that leave too little
// text width; check_paragraph refuses them when they leave none.
void paragraph_ok()
{
  using writeit::check_paragraph;
  using writeit::Indents;
  using writeit::ParaField;
  using writeit::Units;
  const Units in = Units::Inches;
  const Units cm = Units::Centimetres;
  const std::string sides_in =
      "The left and right indents are too large for the 6.98\" text area. The text cannot fit "
      "between them.";
  const std::string first_in =
      "The left, first-line and right indents are too large for the 6.98\" text area. The first "
      "line cannot fit between them.";
  const std::string sides_cm =
      "The left and right indents are too large for the 17.73 cm text area. The text cannot fit "
      "between them.";
  const std::string hang =
      "The hanging indent is larger than the left indent. The first line cannot start to the "
      "left of the margin.";
  const std::string range_in = "The measurement must be between 0\" and 22\".";
  const std::string range_cm = "The measurement must be between 0 cm and 55.88 cm.";
  const std::string invalid_in =
      "This is not a valid measurement. The measurement must be between 0\" and 22\".";

  // The text area is A4 less the page view's margins, as the screen lays it out.
  CHECK(writeit::kTextWidthTwips == 10054);
  CHECK(writeit::twips_to_px(writeit::kTextWidthTwips, 1.0) ==
        writeit::page_widths(writeit::ViewMode::Page, 1.0).wrap);
  CHECK(writeit::twips_to_px(writeit::kTextWidthTwips, 2.0) ==
        writeit::page_widths(writeit::ViewMode::Draft, 2.0).wrap);

  // OK on an unchanged dialog applies what the paragraph had, twip for twip.
  {
    const auto c = check_paragraph(fields(Indents{1410, 567, -360}, in, "0.98\"", "0.39\"",
                                          "0.25\"", 2));
    CHECK(c.field == ParaField::None && c.message.empty());
    CHECK(c.indents == (Indents{1410, 567, -360}));
  }
  // Edits apply in the unit.
  {
    const auto c = check_paragraph(fields(Indents{}, cm, "2.54", "1.27 cm", "1\"", 1));
    CHECK(c.field == ParaField::None);
    CHECK(c.indents == (Indents{1440, 720, 1440}));
  }

  // Bug Basher's defect 3: Left 22" and Right 22".
  {
    const auto c = check_paragraph(fields(Indents{}, in, "22", "22", "0\"", 0));
    CHECK(c.field == ParaField::Right);
    CHECK(c.message == sides_in);
  }
  // Right is at fault unless only Left changed.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "22", "0\"", "0\"", 0));
    CHECK(c.field == ParaField::Left && c.message == sides_in);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, in, "0\"", "7", "0\"", 0));
    CHECK(c.field == ParaField::Right && c.message == sides_in);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, cm, "10", "10", "0 cm", 0));
    CHECK(c.field == ParaField::Right && c.message == sides_cm);
  }
  // A file that already says \li31680\ri31680 cannot be OKed unchanged.
  {
    const Indents wide{writeit::kMaxIndent, writeit::kMaxIndent, 0};
    const auto c = check_paragraph(fields(wide, in, "22\"", "22\"", "0\"", 0));
    CHECK(c.field == ParaField::Right && c.message == sides_in);
  }
  // Just inside and just at the text area: 3.49" + 3.49" is 10052 twips,
  // 3.5" + 3.49" is 10066.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "3.49", "3.49", "0\"", 0));
    CHECK(c.field == ParaField::None && c.indents == (Indents{5026, 5026, 0}));
  }
  {
    const auto c = check_paragraph(fields(Indents{}, in, "3.5", "3.49", "0\"", 0));
    CHECK(c.field == ParaField::Right && c.message == sides_in);
  }
  // At a twip: indents adding up to the width are refused, one twip less is not.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "1", "1", "0\"", 0), 2880);
    CHECK(c.field == ParaField::Right);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, in, "1", "1", "0\"", 0), 2881);
    CHECK(c.field == ParaField::None && c.indents == (Indents{1440, 1440, 0}));
  }
  // A first-line indent counts: its line starts that much further right.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "3", "3", "1", 1));
    CHECK(c.field == ParaField::By && c.message == first_in);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, in, "3", "3", "0.98", 1));
    CHECK(c.field == ParaField::None && c.indents == (Indents{4320, 4320, 1411}));
  }
  // ...but when Left and Right alone leave no room, they are the fault.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "4", "4", "1", 1));
    CHECK(c.field == ParaField::Right && c.message == sides_in);
  }
  // A hanging indent gives the first line more room, not less.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "3.4", "3.4", "1", 2));
    CHECK(c.field == ParaField::None && c.indents == (Indents{4896, 4896, -1440}));
  }
  // The hanging-indent warning is unchanged, and comes first.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "0.5", "0\"", "1", 2));
    CHECK(c.field == ParaField::By && c.message == hang);
  }
  {
    const auto c = check_paragraph(fields(Indents{1440, 0, -1440}, in, "0.5", "7", "1\"", 2));
    CHECK(c.field == ParaField::Left && c.message == hang);
  }

  // Bug Basher's defect 4: 99, 30 and 1e3, each with its message, Left first.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "99", "0\"", "0\"", 0));
    CHECK(c.field == ParaField::Left && c.message == range_in);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, in, "1", "30", "0\"", 0));
    CHECK(c.field == ParaField::Right && c.message == range_in);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, in, "1", "1", "1e3", 1));
    CHECK(c.field == ParaField::By && c.message == invalid_in);
  }
  {
    const auto c = check_paragraph(fields(Indents{}, cm, "99", "abc", "0 cm", 0));
    CHECK(c.field == ParaField::Left && c.message == range_cm);
  }
  // A field is checked before the indents it makes: 99" is out of range,
  // not too wide.
  {
    const auto c = check_paragraph(fields(Indents{}, in, "5", "99", "0\"", 0));
    CHECK(c.field == ParaField::Right && c.message == range_in);
  }
  // By is not read while Special is (none).
  {
    const auto c = check_paragraph(fields(Indents{}, in, "1", "0\"", "1e3", 0));
    CHECK(c.field == ParaField::None && c.indents == (Indents{1440, 0, 0}));
  }
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 9980;

int main()
{
  model();
  rtf_write();
  rtf_round_trip();
  rtf_read();
  markdown();
  dialog_validation();
  field_ranges();
  paragraph_ok();
  return suite_test::done("indents", kChecks);
}
