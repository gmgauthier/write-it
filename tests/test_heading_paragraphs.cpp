/* SPDX-License-Identifier: Unlicense */

// An empty paragraph straight after a heading is body text. The RTF reader
// used to hand it the heading's level, because \outlinelevel was still in
// force when the heading's \par opened the next paragraph.

#include "check.hpp"
#include "document.hpp"

#include <string>

namespace {

writeit::Paragraph para(const char* text, int heading)
{
  writeit::Paragraph paragraph;
  paragraph.heading = heading;
  if (text[0] != '\0') {
    writeit::Run run;
    run.text = text;
    paragraph.runs.push_back(run);
  }
  return paragraph;
}

writeit::Document doc_of(std::initializer_list<writeit::Paragraph> paragraphs)
{
  writeit::Document doc;
  doc.paragraphs = paragraphs;
  // As a document holds headings since named styles: in their Heading
  // styles. Sheet-less headings are M1's, which the reader upgrades so.
  writeit::adopt_heading_styles(doc, "Sans", 11);
  return doc;
}

writeit::Document rtf_round_trip(const writeit::Document& doc)
{
  writeit::Document back;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), back));
  return back;
}

std::string levels(const writeit::Document& doc)
{
  std::string out;
  for (const auto& paragraph : doc.paragraphs)
    out += std::to_string(paragraph.heading) + (paragraph.runs.empty() ? "e" : "t") + " ";
  return out;
}

const std::string kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}";

writeit::Document import(const std::string& body)
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(kHead + body + "}", doc));
  return doc;
}

void rtf()
{
  // Heading, then an empty paragraph.
  const auto a = doc_of({para("Head", 2), para("", 0)});
  CHECK(levels(rtf_round_trip(a)) == "2t 0e ");
  CHECK(rtf_round_trip(a) == a);
  // Heading, empty, body.
  const auto b = doc_of({para("Head", 1), para("", 0), para("Body", 0)});
  CHECK(levels(rtf_round_trip(b)) == "1t 0e 0t ");
  CHECK(rtf_round_trip(b) == b);
  // Several empties, and a second heading after them.
  const auto c = doc_of({para("One", 1), para("", 0), para("", 0), para("Two", 3), para("", 0)});
  CHECK(levels(rtf_round_trip(c)) == "1t 0e 0e 3t 0e ");
  CHECK(rtf_round_trip(c) == c);
  // A heading at the end of the document, and a document that is one heading.
  const auto d = doc_of({para("Body", 0), para("Head", 3)});
  CHECK(rtf_round_trip(d) == d);
  const auto e = doc_of({para("Only", 6)});
  CHECK(rtf_round_trip(e) == e);
  // Heading after an empty body paragraph still reads as a heading.
  const auto f = doc_of({para("", 0), para("Head", 2), para("Body", 0)});
  CHECK(rtf_round_trip(f) == f);
  // The writer is right already: an empty body paragraph resets with \pard
  // and carries no \outlinelevel.
  const std::string rtf = writeit::rtf_export(a);
  CHECK(rtf.find("\\pard\\par") != std::string::npos);
  // Twice round is the same as once.
  CHECK(rtf_round_trip(rtf_round_trip(b)) == b);
}

void rtf_read()
{
  // What a file says at the empty paragraph's \par decides.
  CHECK(levels(import("\\pard\\outlinelevel1 Head\\par\\pard\\par")) == "2t 0e ");
  CHECK(levels(import("\\pard\\outlinelevel0 Head\\par\\pard\\par\\pard Body\\par")) ==
        "1t 0e 0t ");
  // Without \pard, paragraph properties carry on in RTF, so an empty
  // paragraph that is still at \outlinelevel1 is an (empty) heading.
  CHECK(levels(import("\\pard\\outlinelevel1 Head\\par\\par")) == "2t 2e ");
  // Groups scope \outlinelevel like any paragraph property.
  CHECK(levels(import("{\\pard\\outlinelevel2 Head\\par}\\par")) == "3t 0e ");
}

void markdown()
{
  // Markdown has no empty paragraphs, so the empty one is dropped, but no
  // paragraph comes back as a heading it was not.
  const auto a = doc_of({para("Head", 2), para("", 0)});
  CHECK(levels(writeit::markdown_import(writeit::markdown_export(a), "Sans", 11)) == "2t ");
  const auto b = doc_of({para("Head", 1), para("", 0), para("Body", 0)});
  CHECK(levels(writeit::markdown_import(writeit::markdown_export(b), "Sans", 11)) == "1t 0t ");
  const auto d = doc_of({para("Body", 0), para("Head", 3)});
  CHECK(levels(writeit::markdown_import(writeit::markdown_export(d), "Sans", 11)) == "0t 3t ");
}

}  // namespace

int main()
{
  rtf();
  rtf_read();
  markdown();
  return suite_test::done("heading-paragraphs");
}
