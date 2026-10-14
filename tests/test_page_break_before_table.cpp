/* SPDX-License-Identifier: Unlicense */

// A page break at the end of the paragraph before a table used to flag the
// first cell. The sheet starts on an empty body paragraph in front of it.

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

void before_the_table()
{
  Document doc;
  doc.paragraphs = {body("Before"), cell("Cell")};
  const int end = writeit::chars_of(doc.paragraphs[0]);
  const int anchor = writeit::chars_before(doc.paragraphs, 1);
  CHECK(end == 6);
  CHECK(anchor == end + 1);
  const int caret = writeit::break_page(doc, end);
  CHECK(doc.paragraphs.size() == 3);
  CHECK(text_of(doc.paragraphs[0]) == "Before");
  CHECK(!doc.paragraphs[0].page_break);
  CHECK(text_of(doc.paragraphs[1]).empty());
  CHECK(doc.paragraphs[1].runs.empty());
  CHECK(doc.paragraphs[1].cell.table == 0);
  CHECK(doc.paragraphs[1].page_break);
  CHECK(doc.paragraphs[1].page_break && doc.paragraphs[1].cell.table == 0);
  CHECK(text_of(doc.paragraphs[2]) == "Cell");
  CHECK(doc.paragraphs[2].cell.table != 0);
  CHECK(!doc.paragraphs[2].page_break);
  CHECK(caret == writeit::chars_before(doc.paragraphs, 1));
  CHECK(writeit::break_page(doc, end) == -1);
  CHECK(doc.paragraphs.size() == 3);
  CHECK(!doc.paragraphs[2].page_break);
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
  CHECK(between.paragraphs[1].cell.table == 0);
  CHECK(moved == writeit::chars_before(between.paragraphs, 1));

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
  before_the_table();
  body_breaks();
  return suite_test::done("page-break-before-table", 42);
}
