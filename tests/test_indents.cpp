/* SPDX-License-Identifier: Unlicense */

// M2 paragraph indents: the model, RTF write and read (\li \ri \fi), and the
// edge cases a hand-written or Word-written file can carry.

#include "check.hpp"
#include "document.hpp"
#include "units.hpp"

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

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 1594;

int main()
{
  model();
  rtf_write();
  rtf_round_trip();
  rtf_read();
  markdown();
  dialog_validation();
  return suite_test::done("indents", kChecks);
}
