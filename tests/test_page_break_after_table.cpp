/* SPDX-License-Identifier: Unlicense */

// A caret just after a table that ends the document used to be clamped into
// the last cell. The break has to be a body paragraph, or no sheet starts.

#include "check.hpp"
#include "document.hpp"

#include <string>

namespace {

using writeit::Document;
using writeit::Paragraph;
using writeit::Run;

std::string text_of(const Paragraph& paragraph)
{
  std::string out;
  for (const Run& run : paragraph.runs)
    out += run.text;
  return out;
}

Paragraph body(const std::string& text)
{
  Paragraph paragraph;
  if (!text.empty()) {
    Run run;
    run.text = text;
    paragraph.runs.push_back(std::move(run));
  }
  return paragraph;
}

Paragraph cell(const std::string& text)
{
  Paragraph paragraph = body(text);
  paragraph.cell.table = 1;
  paragraph.cell.row = 0;
  paragraph.cell.column = 0;
  paragraph.cell.rows = 1;
  paragraph.cell.columns = 1;
  paragraph.cell.widths.push_back(1000);
  return paragraph;
}

// A table is one anchor character. The cell's own text is not in the buffer.
void table_anchor()
{
  Document doc;
  doc.paragraphs = {body("Before"), cell("Cell")};
  const int anchor = writeit::chars_before(doc.paragraphs, 1);
  CHECK(anchor == writeit::chars_of(doc.paragraphs[0]) + 1);
  CHECK(writeit::chars_of(doc.paragraphs[1]) == 4);
  CHECK(writeit::chars_before(doc.paragraphs, doc.paragraphs.size()) == anchor + 1);
}

void after_the_table()
{
  Document doc;
  doc.paragraphs = {body("Before"), cell("Cell")};
  const int anchor = writeit::chars_before(doc.paragraphs, 1);
  const int caret = writeit::break_page(doc, anchor + 1);
  CHECK(doc.paragraphs.size() == 3);
  CHECK(text_of(doc.paragraphs[0]) == "Before");
  CHECK(!doc.paragraphs[0].page_break);
  CHECK(text_of(doc.paragraphs[1]) == "Cell");
  CHECK(doc.paragraphs[1].cell.table != 0);
  CHECK(!doc.paragraphs[1].page_break);
  CHECK(doc.paragraphs[2].cell.table == 0);
  CHECK(doc.paragraphs[2].page_break);
  CHECK(doc.paragraphs[2].page_break && doc.paragraphs[2].cell.table == 0);
  CHECK(text_of(doc.paragraphs[2]).empty());
  CHECK(caret == writeit::chars_before(doc.paragraphs, 2));
}

// An empty cell must not merely take the flag. The break is still a body paragraph.
void after_an_empty_cell()
{
  Document doc;
  doc.paragraphs = {body("Before"), cell("")};
  const int anchor = writeit::chars_before(doc.paragraphs, 1);
  CHECK(writeit::chars_of(doc.paragraphs[1]) == 0);
  const int caret = writeit::break_page(doc, anchor + 1);
  CHECK(doc.paragraphs.size() == 3);
  CHECK(text_of(doc.paragraphs[1]).empty());
  CHECK(doc.paragraphs[1].cell.table != 0);
  CHECK(!doc.paragraphs[1].page_break);
  CHECK(doc.paragraphs[2].cell.table == 0);
  CHECK(doc.paragraphs[2].page_break);
  CHECK(caret == writeit::chars_before(doc.paragraphs, 2));
}

// On the anchor the caret is still the cell. The window refuses that before
// break_page; the function must not treat it as a caret past the table.
void on_the_anchor()
{
  Document doc;
  doc.paragraphs = {body("Before"), cell("Cell")};
  const int anchor = writeit::chars_before(doc.paragraphs, 1);
  const int caret = writeit::break_page(doc, anchor);
  CHECK(doc.paragraphs.size() == 2);
  CHECK(text_of(doc.paragraphs[1]) == "Cell");
  CHECK(doc.paragraphs[1].cell.table != 0);
  CHECK(doc.paragraphs[1].page_break);
  CHECK(caret == anchor);
}

void body_breaks()
{
  Document start;
  start.paragraphs = {body("Hello")};
  CHECK(writeit::break_page(start, 0) == 0);
  CHECK(start.paragraphs.size() == 1);
  CHECK(start.paragraphs[0].page_break);
  CHECK(text_of(start.paragraphs[0]) == "Hello");
  CHECK(writeit::break_page(start, 0) == -1);

  Document middle;
  middle.paragraphs = {body("Hello")};
  const int split = writeit::break_page(middle, 2);
  CHECK(middle.paragraphs.size() == 2);
  CHECK(text_of(middle.paragraphs[0]) == "He");
  CHECK(!middle.paragraphs[0].page_break);
  CHECK(text_of(middle.paragraphs[1]) == "llo");
  CHECK(middle.paragraphs[1].page_break);
  CHECK(split == writeit::chars_before(middle.paragraphs, 1));

  Document between;
  between.paragraphs = {body("Hello"), body("World")};
  const int end = writeit::chars_of(between.paragraphs[0]);
  const int moved = writeit::break_page(between, end);
  CHECK(between.paragraphs.size() == 2);
  CHECK(text_of(between.paragraphs[0]) == "Hello");
  CHECK(!between.paragraphs[0].page_break);
  CHECK(text_of(between.paragraphs[1]) == "World");
  CHECK(between.paragraphs[1].page_break);
  CHECK(moved == writeit::chars_before(between.paragraphs, 1));
  CHECK(writeit::break_page(between, end) == -1);

  Document last;
  last.paragraphs = {body("Hello")};
  const int added = writeit::break_page(last, writeit::chars_of(last.paragraphs[0]));
  CHECK(last.paragraphs.size() == 2);
  CHECK(text_of(last.paragraphs[0]) == "Hello");
  CHECK(!last.paragraphs[0].page_break);
  CHECK(last.paragraphs[1].cell.table == 0);
  CHECK(last.paragraphs[1].page_break);
  CHECK(text_of(last.paragraphs[1]).empty());
  CHECK(added == writeit::chars_before(last.paragraphs, 1));
}

}  // namespace

int main()
{
  table_anchor();
  after_the_table();
  after_an_empty_cell();
  on_the_anchor();
  body_breaks();
  return suite_test::done("page-break-after-table", 52);
}
