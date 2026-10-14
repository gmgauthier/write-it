/* SPDX-License-Identifier: Unlicense */

// Where a footnote marker sits in the body. A marker in ordinary text is that
// run. A note that is only inside a table is the table's anchor, which is the
// one character the cells share, so the sheet that holds the table holds the
// note. A missing marker is not a place.

#include "check.hpp"
#include "document.hpp"

namespace {

writeit::Run text_run(const char* text, int note = 0)
{
  writeit::Run run;
  run.text = text;
  run.note = note;
  return run;
}

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

void cell_only()
{
  std::vector<writeit::Paragraph> paragraphs;
  paragraphs.push_back(body("Hello"));
  CHECK(writeit::insert_table(paragraphs, paragraphs.size(), 1, 1, 4000));
  paragraphs.back().runs.push_back(text_run("1", 1));
  // "Hello" plus its newline, then the anchor.
  CHECK(writeit::note_marker_offset(paragraphs, 1) == 6);
  CHECK(writeit::note_marker_offset(paragraphs, 1) != -1);
  CHECK(writeit::note_marker_offset(paragraphs, 1) ==
        writeit::chars_before(paragraphs, 1));
}

void body_marker()
{
  std::vector<writeit::Paragraph> paragraphs;
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run("Hi"));
  paragraph.runs.push_back(text_run("1", 1));
  paragraphs.push_back(paragraph);
  CHECK(writeit::insert_table(paragraphs, paragraphs.size(), 1, 1, 4000));
  paragraphs.back().runs.push_back(text_run("cell", 1));
  // The body run, not the anchor that follows the paragraph.
  CHECK(writeit::note_marker_offset(paragraphs, 1) == 2);
  CHECK(writeit::note_marker_offset(paragraphs, 1) != writeit::chars_before(paragraphs, 1));
}

void no_marker()
{
  std::vector<writeit::Paragraph> paragraphs;
  paragraphs.push_back(body("Hello"));
  CHECK(writeit::insert_table(paragraphs, paragraphs.size(), 1, 2, 4000));
  paragraphs[1].runs.push_back(text_run("plain"));
  CHECK(writeit::note_marker_offset(paragraphs, 1) == -1);
  CHECK(writeit::note_marker_offset(paragraphs, 0) == -1);
  CHECK(writeit::note_marker_offset(paragraphs, -1) == -1);
}

void second_cell()
{
  std::vector<writeit::Paragraph> paragraphs;
  paragraphs.push_back(body("Hello"));
  CHECK(writeit::insert_table(paragraphs, paragraphs.size(), 1, 2, 4000));
  // Cells share the anchor. The note is in the second column.
  CHECK(paragraphs.size() == 3);
  paragraphs[2].runs.push_back(text_run("2", 1));
  const int anchor = writeit::chars_before(paragraphs, 1);
  CHECK(anchor == 6);
  CHECK(writeit::note_marker_offset(paragraphs, 1) == anchor);
  CHECK(writeit::chars_before(paragraphs, 2) == anchor);
}

void after_table()
{
  std::vector<writeit::Paragraph> paragraphs;
  paragraphs.push_back(body("Hi"));
  CHECK(writeit::insert_table(paragraphs, paragraphs.size(), 1, 1, 4000));
  writeit::Paragraph tail;
  tail.runs.push_back(text_run("X"));
  tail.runs.push_back(text_run("1", 1));
  paragraphs.push_back(tail);
  // "Hi" and its newline, the anchor, and the separator newline, then "X".
  CHECK(writeit::note_marker_offset(paragraphs, 1) == 6);
  CHECK(writeit::note_marker_offset(paragraphs, 2) == -1);
}

}  // namespace

// Exactly the checks this suite runs.
constexpr int kChecks = 19;

int main()
{
  cell_only();
  body_marker();
  no_marker();
  second_cell();
  after_table();
  return suite_test::done("cell-footnote", kChecks);
}
