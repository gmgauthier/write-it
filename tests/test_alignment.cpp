/* SPDX-License-Identifier: Unlicense */

// M2 paragraph alignment: Left, Center, Right and Justify, as Word 97, in the
// model and in RTF (\ql \qc \qr \qj), and alongside indents.

#include "check.hpp"
#include "document.hpp"

#include <string>

namespace {

using writeit::Align;

writeit::Paragraph para(const char* text, Align align, int left = 0, int right = 0, int first = 0)
{
  writeit::Paragraph paragraph;
  paragraph.align = align;
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

const std::string kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}";

writeit::Document import(const std::string& body)
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(kHead + body + "}", doc));
  return doc;
}

Align align_of(const writeit::Document& doc, size_t index)
{
  if (index >= doc.paragraphs.size())
    return static_cast<Align>(-1);
  return doc.paragraphs[index].align;
}

void model()
{
  writeit::Paragraph plain;
  CHECK(plain.align == Align::Left);
  CHECK(para("a", Align::Center) == para("a", Align::Center));
  CHECK(!(para("a", Align::Center) == para("a", Align::Left)));
  CHECK(!(para("a", Align::Right) == para("a", Align::Center)));
  CHECK(para("a", Align::Justify) == para("a", Align::Justify));
  CHECK(!(para("a", Align::Justify) == para("a", Align::Left)));
  CHECK(!(para("a", Align::Justify) == para("a", Align::Right)));
  writeit::Document a;
  writeit::Document b;
  a.paragraphs.push_back(para("x", Align::Left));
  b.paragraphs.push_back(para("x", Align::Right));
  CHECK(!(a == b));
  b.paragraphs[0].align = Align::Left;
  CHECK(a == b);
  // A blank document and imported text start left-aligned.
  CHECK(writeit::blank_document("Sans", 11).paragraphs[0].align == Align::Left);
  CHECK(writeit::plain_import("one\ntwo", "Sans", 11).paragraphs[1].align == Align::Left);
  CHECK(writeit::markdown_import("# Head\n\nBody", "Sans", 11).paragraphs[0].align == Align::Left);
}

void rtf_write()
{
  writeit::Document doc;
  doc.paragraphs.push_back(para("Left", Align::Left));
  doc.paragraphs.push_back(para("Centre", Align::Center));
  doc.paragraphs.push_back(para("Right", Align::Right));
  doc.paragraphs.push_back(para("Both", Align::Center, 720, 360, -360));
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(contains(rtf, "\\pard\\qc"));
  CHECK(contains(rtf, "\\pard\\qr"));
  CHECK(contains(rtf, "\\pard\\li720\\ri360\\fi-360\\qc"));
  // Left is the default and is not written.
  CHECK(!contains(rtf, "\\ql"));
  CHECK(!contains(rtf, "\\qj"));
  CHECK(contains(rtf, "\\pard\\f0"));
  // Justify writes \qj, after the indents like the others.
  writeit::Document justified;
  justified.paragraphs.push_back(para("Justified", Align::Justify));
  justified.paragraphs.push_back(para("Indented", Align::Justify, 720, 0, 360));
  const std::string qj = writeit::rtf_export(justified);
  CHECK(contains(qj, "\\pard\\qj"));
  CHECK(contains(qj, "\\pard\\li720\\fi360\\qj"));
  CHECK(!contains(qj, "\\ql"));
}

void rtf_round_trip()
{
  writeit::Document doc;
  doc.paragraphs.push_back(para("Left", Align::Left));
  doc.paragraphs.push_back(para("Centre", Align::Center));
  doc.paragraphs.push_back(para("Right", Align::Right));
  doc.paragraphs.push_back(para("Centre indented", Align::Center, 1440, 720, 360));
  doc.paragraphs.push_back(para("Right hanging", Align::Right, 720, 0, -720));
  doc.paragraphs.push_back(para("", Align::Center));
  writeit::Paragraph heading = para("Heading", Align::Right);
  heading.heading = 2;
  doc.paragraphs.push_back(heading);
  doc.paragraphs.push_back(para("End", Align::Right));
  doc.paragraphs.push_back(para("Justified", Align::Justify));
  doc.paragraphs.push_back(para("Justified hanging", Align::Justify, 720, 360, -360));
  doc.paragraphs.push_back(para("", Align::Justify));
  // The heading in its Heading style, as since named styles.
  writeit::adopt_heading_styles(doc, "Sans", 11);
  writeit::Document back;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), back));
  CHECK(back == doc);
  CHECK(back.paragraphs.size() == doc.paragraphs.size());
  for (size_t i = 0; i < doc.paragraphs.size() && i < back.paragraphs.size(); ++i)
    CHECK(back.paragraphs[i].align == doc.paragraphs[i].align);
  // Twice is the same as once.
  writeit::Document again;
  CHECK(writeit::rtf_import(writeit::rtf_export(back), again));
  CHECK(again == doc);
}

void rtf_read()
{
  // Each word. \qd (distributed, from East Asian Word) has no Word 97
  // button and reads as left.
  CHECK(align_of(import("\\pard\\ql A\\par"), 0) == Align::Left);
  CHECK(align_of(import("\\pard\\qc A\\par"), 0) == Align::Center);
  CHECK(align_of(import("\\pard\\qr A\\par"), 0) == Align::Right);
  CHECK(align_of(import("\\pard\\qj A\\par"), 0) == Align::Justify);
  CHECK(align_of(import("\\pard\\qd A\\par"), 0) == Align::Left);
  CHECK(align_of(import("\\pard\\qj\\ql A\\par"), 0) == Align::Left);
  CHECK(align_of(import("\\pard\\qc\\qj A\\par"), 0) == Align::Justify);
  // \pard resets justified, and it carries until then.
  {
    const auto doc = import("\\pard\\qj A\\par B\\par\\pard C\\par");
    CHECK(align_of(doc, 0) == Align::Justify);
    CHECK(align_of(doc, 1) == Align::Justify);
    CHECK(align_of(doc, 2) == Align::Left);
  }
  CHECK(align_of(import("{\\qj A\\par}B\\par"), 1) == Align::Left);
  // The last word wins.
  CHECK(align_of(import("\\pard\\qc\\qr A\\par"), 0) == Align::Right);
  CHECK(align_of(import("\\pard\\qr\\ql A\\par"), 0) == Align::Left);
  // Paragraph properties carry to the next paragraph until \pard resets them.
  {
    const auto doc = import("\\pard\\qc A\\par B\\par\\pard C\\par");
    CHECK(align_of(doc, 0) == Align::Center);
    CHECK(align_of(doc, 1) == Align::Center);
    CHECK(align_of(doc, 2) == Align::Left);
  }
  // Groups scope them.
  {
    const auto doc = import("{\\qr A\\par}B\\par");
    CHECK(align_of(doc, 0) == Align::Right);
    CHECK(align_of(doc, 1) == Align::Left);
  }
  CHECK(align_of(import("\\pard\\qc A{\\qr}\\par"), 0) == Align::Center);
  // The value at \par counts, as for indents.
  CHECK(align_of(import("\\pard A\\qc\\par"), 0) == Align::Center);
  // A last paragraph with no \par takes the value at the closing brace.
  CHECK(align_of(import("\\pard\\qr A"), 0) == Align::Right);
  {
    const auto doc = import("\\pard\\qc A\\par\\pard\\qr B");
    CHECK(align_of(doc, 0) == Align::Center);
    CHECK(align_of(doc, 1) == Align::Right);
  }
  // Alignment does not disturb indents, and the reverse.
  {
    const auto doc = import("\\pard\\li720\\fi-360\\qr A\\par\\pard\\qc\\li1440 B\\par");
    CHECK(align_of(doc, 0) == Align::Right);
    CHECK(doc.paragraphs[0].indents.left == 720);
    CHECK(doc.paragraphs[0].indents.first == -360);
    CHECK(align_of(doc, 1) == Align::Center);
    CHECK(doc.paragraphs[1].indents.left == 1440);
    CHECK(doc.paragraphs[1].indents.first == 0);
  }
  // Ignored destinations do not leak alignment.
  CHECK(align_of(import("{\\*\\generator \\qr x;}\\pard A\\par"), 0) == Align::Left);
  // A \qj file keeps justification through a save and reopen.
  {
    writeit::Document back;
    CHECK(writeit::rtf_import(writeit::rtf_export(import("\\pard\\qj A\\par")), back));
    CHECK(align_of(back, 0) == Align::Justify);
  }
}

// Justified paragraphs as Word 97 and LibreOffice Writer write them: \plain,
// a style number and a run of other paragraph words around \qj.
void foreign_files()
{
  const std::string word97 =
      "{\\rtf1\\ansi\\ansicpg1252\\uc1 \\deff0\\deflang1033\\deflangfe1033"
      "{\\fonttbl{\\f0\\froman\\fcharset0\\fprq2{\\*\\panose 02020603050405020304}Times New Roman;}}"
      "{\\stylesheet{\\widctlpar\\adjustright \\fs20\\cgrid \\snext0 Normal;}}"
      "{\\info{\\author Greg}{\\operator Greg}}"
      "\\widowctrl\\ftnbj\\aenddoc\\formshade\\viewkind1\\viewscale100 \\fet0\\sectd "
      "\\linex0\\endnhere\\sectdefaultcl "
      "\\pard\\plain \\qj \\widctlpar\\adjustright \\fs20\\cgrid {Justified in Word 97, long "
      "enough to wrap.\\par }"
      "\\pard \\qj \\li720\\widctlpar\\adjustright {Indented and justified.\\par }"
      "\\pard \\widctlpar\\adjustright {Left again.\\par }"
      "\\pard \\qc \\widctlpar\\adjustright {Centred.}}";
  writeit::Document word;
  CHECK(writeit::rtf_import(word97, word));
  CHECK(word.paragraphs.size() == 4);
  CHECK(align_of(word, 0) == Align::Justify);
  CHECK(align_of(word, 1) == Align::Justify);
  CHECK(word.paragraphs.size() > 1 && word.paragraphs[1].indents.left == 720);
  CHECK(align_of(word, 2) == Align::Left);
  CHECK(align_of(word, 3) == Align::Center);

  const std::string writer =
      "{\\rtf1\\ansi\\deff3\\adeflang1025"
      "{\\fonttbl{\\f0\\froman\\fprq2\\fcharset0 Times New Roman;}{\\f3\\fswiss\\fprq2\\fcharset0 "
      "Liberation Sans;}}"
      "{\\stylesheet{\\s0\\snext0\\ql\\widctlpar\\hyphpar0\\ltrpar\\cf0\\loch\\f3\\fs24\\lang2057 "
      "Normal;}}"
      "{\\*\\generator LibreOffice/24.2.7.2$Linux_X86_64 LibreOffice_project/420$Build-2}"
      "\\formshade\\paperh16838\\paperw11906\\margl1134\\margr1134\\margt1134\\margb1134\\sectd"
      "\\sbknone\\sftnnar\\saftnnrlc\\sectunlocked1\\pgwsxn11906\\pghsxn16838\\marglsxn1134"
      "\\margrsxn1134\\margtsxn1134\\margbsxn1134\\ftnbj\\ftnstart1\\ftnrstcont\\ftnnar\\aenddoc"
      "\\aftnrstcont\\aftnstart1\\aftnnrlc\\htmautsp"
      "{\\*\\ftnsep\\chftnsep}"
      "\\pgndec\\pard\\plain \\s0\\qj\\widctlpar\\hyphpar0\\ltrpar\\cf0\\loch\\f3\\fs24\\lang2057"
      "{\\loch Justified in Writer.}\\par "
      "\\pard\\plain \\s0\\ql\\widctlpar\\hyphpar0\\ltrpar\\cf0\\loch\\f3\\fs24\\lang2057"
      "{\\loch Left.}\\par "
      "\\pard\\plain \\s0\\qj\\widctlpar\\hyphpar0\\ltrpar\\cf0\\loch\\f3\\fs24\\lang2057"
      "{\\loch Justified last.}\\par }";
  writeit::Document lo;
  CHECK(writeit::rtf_import(writer, lo));
  CHECK(align_of(lo, 0) == Align::Justify);
  CHECK(align_of(lo, 1) == Align::Left);
  CHECK(align_of(lo, 2) == Align::Justify);

  // And Word's file saves and reopens with its justification.
  writeit::Document back;
  CHECK(writeit::rtf_import(writeit::rtf_export(word), back));
  CHECK(align_of(back, 0) == Align::Justify);
  CHECK(align_of(back, 1) == Align::Justify);
  CHECK(back == word);
}

void markdown()
{
  // Markdown has no alignment; export drops it and does not fail.
  writeit::Document doc;
  doc.paragraphs.push_back(para("Centre", Align::Center));
  CHECK(writeit::markdown_export(doc) == writeit::markdown_export(
                                             [] {
                                               writeit::Document plain;
                                               plain.paragraphs.push_back(para("Centre", Align::Left));
                                               return plain;
                                             }()));
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 105;

int main()
{
  model();
  rtf_write();
  rtf_round_trip();
  rtf_read();
  foreign_files();
  markdown();
  return suite_test::done("alignment", kChecks);
}
