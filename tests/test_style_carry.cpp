/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 10;

int main()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::Paragraph body;
  body.style = "Heading 1";
  body.heading = 1;
  writeit::Run run;
  run.text = "Title";
  run.font = "Sans";
  run.size = 16;
  run.bold = true;
  body.runs.push_back(run);
  doc.paragraphs = {body};
  writeit::Paragraph header = body;
  header.runs[0].text = "Head";
  doc.header = {header};
  writeit::Paragraph footer = body;
  footer.runs[0].text = "Foot";
  doc.footer = {footer};
  writeit::Paragraph note = body;
  note.runs[0].text = "Note";
  doc.notes = {{note}};

  const writeit::Style* found = writeit::find_style(writeit::style_sheet(doc), "Heading 1");
  CHECK(found != nullptr);
  if (!found)
    return suite_test::done("style-carry", kChecks);
  writeit::Style changed = *found;
  changed.format.italic = true;
  CHECK(writeit::update_style(doc, "Heading 1", changed));
  CHECK(doc.paragraphs[0].runs[0].italic);
  CHECK(doc.header[0].runs[0].italic && doc.footer[0].runs[0].italic);
  CHECK(doc.notes[0][0].runs[0].italic);

  // A file stores italic with no direct bit. Restyling a cell to Heading 1
  // keeps it, because it differs from Normal. Taking the style clears it.
  writeit::Document fresh = writeit::blank_document("Sans", 11);
  const std::vector<writeit::Style> sheet = writeit::style_sheet(fresh);
  const writeit::Style* heading = writeit::find_style(sheet, "Heading 1");
  CHECK(heading != nullptr);
  writeit::Paragraph cell;
  cell.style = "Normal";
  writeit::Run kept;
  kept.text = "Kept";
  kept.italic = true;
  cell.runs.push_back(kept);
  writeit::Paragraph taken = cell;
  if (heading)
    writeit::take_paragraph_style(taken, *heading);
  CHECK(heading && !taken.runs[0].italic);
  if (heading)
    writeit::restyle_paragraph_kept(cell, sheet, *heading);
  CHECK(cell.runs[0].italic);
  CHECK(cell.style == "Heading 1");
  CHECK(cell.runs[0].bold);
  return suite_test::done("style-carry", kChecks);
}
