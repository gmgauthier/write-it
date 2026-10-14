/* SPDX-License-Identifier: Unlicense */

// A blank line splits a note. Markers after that note move with it. The
// marker of the note that split stays, and a cell marker moves the same way.

#include "check.hpp"
#include "document.hpp"

namespace {

using writeit::Document;
using writeit::Paragraph;
using writeit::Run;

std::vector<Paragraph> note_of(const std::string& text)
{
  Paragraph paragraph;
  Run run;
  run.text = text;
  paragraph.runs.push_back(std::move(run));
  return {std::move(paragraph)};
}

Paragraph marked(int note, int table)
{
  Paragraph paragraph;
  Run run;
  run.text = std::to_string(note);
  run.note = note;
  paragraph.runs.push_back(std::move(run));
  if (table != 0) {
    paragraph.cell.table = table;
    paragraph.cell.row = 0;
    paragraph.cell.column = 0;
    paragraph.cell.rows = 1;
    paragraph.cell.columns = 1;
  }
  return paragraph;
}

Document three_notes()
{
  Document doc;
  doc.notes = {note_of("A"), note_of("B"), note_of("C")};
  doc.paragraphs = {marked(1, 0), marked(2, 0), marked(3, 0)};
  return doc;
}

// [A, B, C] split at A → [A1, A2, B, C]. Marker 1 stays. 2 and 3 move on.
void split_first()
{
  Document doc = three_notes();
  doc.paragraphs.push_back(marked(2, 1));
  doc.header = {marked(3, 0)};
  doc.footer = {marked(1, 0)};
  const std::vector<std::vector<Paragraph>> before = doc.notes;
  doc.notes = {note_of("A1"), note_of("A2"), note_of("B"), note_of("C")};
  writeit::retarget_note_markers(doc, before);
  CHECK(doc.paragraphs[0].runs[0].note == 1);
  CHECK(doc.paragraphs[1].runs[0].note == 3);
  CHECK(doc.paragraphs[2].runs[0].note == 4);
  CHECK(doc.paragraphs[3].cell.table != 0);
  CHECK(doc.paragraphs[3].runs[0].note == 3);
  CHECK(doc.header[0].runs[0].note == 4);
  CHECK(doc.footer[0].runs[0].note == 1);
}

// [A, B, C] split at B → [A, B1, B2, C]. Markers 1 and 2 stay. 3 moves on.
void split_middle()
{
  Document doc = three_notes();
  doc.paragraphs.push_back(marked(3, 1));
  const std::vector<std::vector<Paragraph>> before = doc.notes;
  doc.notes = {note_of("A"), note_of("B1"), note_of("B2"), note_of("C")};
  writeit::retarget_note_markers(doc, before);
  CHECK(doc.paragraphs[0].runs[0].note == 1);
  CHECK(doc.paragraphs[1].runs[0].note == 2);
  CHECK(doc.paragraphs[2].runs[0].note == 4);
  CHECK(doc.paragraphs[3].cell.table != 0);
  CHECK(doc.paragraphs[3].runs[0].note == 4);
}

// The same number of notes, even with different text, leaves every marker.
void same_count()
{
  Document doc = three_notes();
  doc.paragraphs.push_back(marked(2, 1));
  const std::vector<std::vector<Paragraph>> before = doc.notes;
  doc.notes = {note_of("A"), note_of("B-changed"), note_of("C")};
  writeit::retarget_note_markers(doc, before);
  CHECK(doc.paragraphs[0].runs[0].note == 1);
  CHECK(doc.paragraphs[1].runs[0].note == 2);
  CHECK(doc.paragraphs[2].runs[0].note == 3);
  CHECK(doc.paragraphs[3].runs[0].note == 2);
}

}  // namespace

int main()
{
  split_first();
  split_middle();
  same_count();
  return suite_test::done("note-retarget", 16);
}
