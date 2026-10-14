/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

#include <string>

constexpr int kChecks = 11;

std::string text_of(const writeit::Paragraph& paragraph)
{
  std::string text;
  for (const writeit::Run& run : paragraph.runs) {
    if (run.note == 0)
      text += run.text;
  }
  return text;
}

int main()
{
  writeit::Document chapter;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi Chapter 1\\par{\\footnote The note.}\\par}", chapter));
  CHECK(chapter.paragraphs.size() >= 1);
  CHECK(text_of(chapter.paragraphs[0]) == "Chapter 1");
  CHECK(chapter.notes.size() == 1);
  CHECK(!chapter.notes[0].empty() && !chapter.notes[0][0].runs.empty() &&
        chapter.notes[0][0].runs[0].text == "The note.");

  writeit::Document marked;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi See 1{\\footnote The note.}}", marked));
  CHECK(marked.paragraphs.size() == 1 && marked.paragraphs[0].runs.size() >= 2);
  CHECK(text_of(marked.paragraphs[0]) == "See ");
  CHECK(marked.paragraphs[0].runs.back().note == 1);

  writeit::Document automatic;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi See section 1{\\chftn{\\footnote The note.}}}", automatic));
  CHECK(text_of(automatic.paragraphs[0]) == "See section 1");

  return suite_test::done("rtf-footnote-marker", kChecks);
}
