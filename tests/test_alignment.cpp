/* SPDX-License-Identifier: Unlicense */

// M2 paragraph alignment: Left, Center and Right, in the model and in RTF
// (\ql \qc \qr, with \qj read as left), and alongside indents.

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
  doc.paragraphs.push_back(para("", Align::Right));
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
  // Each word, and \qj read as left: the spec has no justified.
  CHECK(align_of(import("\\pard\\ql A\\par"), 0) == Align::Left);
  CHECK(align_of(import("\\pard\\qc A\\par"), 0) == Align::Center);
  CHECK(align_of(import("\\pard\\qr A\\par"), 0) == Align::Right);
  CHECK(align_of(import("\\pard\\qj A\\par"), 0) == Align::Left);
  CHECK(align_of(import("\\pard\\qd A\\par"), 0) == Align::Left);
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
  // Word's \qj file loses justification on save: it comes back left.
  {
    writeit::Document back;
    CHECK(writeit::rtf_import(writeit::rtf_export(import("\\pard\\qj A\\par")), back));
    CHECK(align_of(back, 0) == Align::Left);
  }
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

int main()
{
  model();
  rtf_write();
  rtf_round_trip();
  rtf_read();
  markdown();
  return suite_test::done("alignment");
}
