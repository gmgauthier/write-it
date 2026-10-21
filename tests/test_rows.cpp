/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

#include <string>
#include <vector>

namespace {

writeit::Run run_text(const char* text, bool bold = false)
{
  writeit::Run run;
  run.text = text;
  run.bold = bold;
  if (bold)
    run.direct = writeit::kDirectBold;
  return run;
}

std::string text_of(const writeit::Paragraph& paragraph)
{
  std::string text;
  for (const writeit::Run& run : paragraph.runs)
    text += run.text;
  return text;
}

const writeit::Paragraph* cell_paragraph(const std::vector<writeit::Paragraph>& paragraphs, int row,
                                         int column, int nth = 0)
{
  int seen = 0;
  for (const writeit::Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    if (seen == nth)
      return &paragraph;
    ++seen;
  }
  return nullptr;
}

int table_rows(const std::vector<writeit::Paragraph>& paragraphs)
{
  for (const writeit::Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != 0)
      return paragraph.cell.rows;
  }
  return 0;
}

int table_columns(const std::vector<writeit::Paragraph>& paragraphs)
{
  for (const writeit::Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != 0)
      return paragraph.cell.columns;
  }
  return 0;
}

writeit::Document make_table(int rows, int columns)
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::insert_table(doc.paragraphs, 1, rows, columns, 4800);
  return doc;
}

void paint(writeit::Paragraph& paragraph, const char* text, bool bold)
{
  paragraph.runs = {run_text(text, bold)};
}

bool same_cells(const writeit::Document& doc, const writeit::Document& again)
{
  if (doc.paragraphs.size() != again.paragraphs.size())
    return false;
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    const writeit::Paragraph& left = doc.paragraphs[i];
    const writeit::Paragraph& right = again.paragraphs[i];
    if (left.cell.table == 0 && right.cell.table == 0) {
      if (text_of(left) != text_of(right))
        return false;
      continue;
    }
    if (left.cell.row != right.cell.row || left.cell.column != right.cell.column)
      return false;
    if (left.cell.rows != right.cell.rows || left.cell.columns != right.cell.columns)
      return false;
    if (text_of(left) != text_of(right))
      return false;
    if (left.runs.size() != right.runs.size())
      return false;
    for (size_t r = 0; r < left.runs.size(); ++r) {
      if (left.runs[r].bold != right.runs[r].bold)
        return false;
    }
    if (left.cell.shading.pattern != right.cell.shading.pattern ||
        left.cell.shading.background != right.cell.shading.background)
      return false;
    if (left.cell.borders_set != right.cell.borders_set)
      return false;
    if (left.indents.left != right.indents.left || left.align != right.align)
      return false;
  }
  return true;
}

}  // namespace

constexpr int kChecks = 70;

int main()
{
  writeit::Document doc = make_table(3, 2);
  paint(doc.paragraphs[1], "A", false);
  paint(doc.paragraphs[2], "B", false);
  paint(doc.paragraphs[3], "C", true);
  doc.paragraphs[3].indents.left = 720;
  doc.paragraphs[3].direct = writeit::kDirectLeft;
  doc.paragraphs[3].align = writeit::Align::Center;
  doc.paragraphs[3].cell.shading.pattern = 10000;
  doc.paragraphs[3].cell.shading.background = writeit::Ink::Yellow;
  doc.paragraphs[3].cell.borders_set = true;
  doc.paragraphs[3].cell.borders.top.style = writeit::BorderStyle::Single;
  doc.paragraphs[3].cell.borders.top.width = 10;
  paint(doc.paragraphs[4], "D", false);
  paint(doc.paragraphs[5], "E", false);
  paint(doc.paragraphs[6], "F", false);
  writeit::Paragraph extra = doc.paragraphs[3];
  extra.runs = {run_text("C2", true)};
  doc.paragraphs.insert(doc.paragraphs.begin() + 4, extra);

  writeit::TableSlice rows;
  CHECK(writeit::copy_table_rows(doc.paragraphs, 3, 1, 1, rows));
  CHECK(rows.band == writeit::TableBand::Rows);
  CHECK(rows.rows == 1 && rows.columns == 2);
  CHECK(text_of(rows.paragraphs[0]) == "C" && rows.paragraphs[0].runs[0].bold);
  CHECK(text_of(rows.paragraphs[1]) == "C2");
  CHECK(text_of(rows.paragraphs[2]) == "D");
  CHECK(rows.paragraphs[0].cell.shading.background == writeit::Ink::Yellow);
  CHECK(rows.paragraphs[0].cell.borders_set);
  CHECK(rows.paragraphs[0].indents.left == 720);
  CHECK(!writeit::copy_table_rows(doc.paragraphs, 0, 0, 0, rows));
  CHECK(!writeit::copy_table_rows(doc.paragraphs, 1, -1, 0, rows));

  writeit::TableSlice both;
  CHECK(writeit::copy_table_rows(doc.paragraphs, 1, 2, 0, both));
  CHECK(both.rows == 3 && both.columns == 2);

  CHECK(writeit::paste_table_rows(doc.paragraphs, 1, rows) == writeit::TablePaste::Placed);
  CHECK(table_rows(doc.paragraphs) == 4);
  const writeit::Paragraph* pasted = cell_paragraph(doc.paragraphs, 1, 0);
  const writeit::Paragraph* pasted_line = cell_paragraph(doc.paragraphs, 1, 0, 1);
  const writeit::Paragraph* pasted_d = cell_paragraph(doc.paragraphs, 1, 1);
  CHECK(pasted && text_of(*pasted) == "C" && pasted->runs[0].bold);
  CHECK(pasted && pasted->cell.shading.pattern == 10000);
  CHECK(pasted && pasted->cell.shading.background == writeit::Ink::Yellow);
  CHECK(pasted && pasted->cell.borders_set && pasted->indents.left == 720);
  CHECK(pasted && pasted->align == writeit::Align::Center);
  CHECK(pasted_line && text_of(*pasted_line) == "C2");
  CHECK(pasted_d && text_of(*pasted_d) == "D");
  const writeit::Paragraph* shifted = cell_paragraph(doc.paragraphs, 2, 0);
  CHECK(shifted && text_of(*shifted) == "C");
  CHECK(cell_paragraph(doc.paragraphs, 3, 0) && text_of(*cell_paragraph(doc.paragraphs, 3, 0)) == "E");

  writeit::Document again;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), again));
  CHECK(same_cells(doc, again));

  writeit::Document wide = make_table(2, 3);
  writeit::TableSlice mismatch;
  CHECK(writeit::copy_table_rows(doc.paragraphs, 1, 0, 0, mismatch));
  const std::vector<writeit::Paragraph> wide_before = wide.paragraphs;
  CHECK(writeit::paste_table_rows(wide.paragraphs, 1, mismatch) == writeit::TablePaste::Misfit);
  CHECK(wide.paragraphs == wide_before);

  writeit::Document full = make_table(31, 1);
  writeit::Document two_src = make_table(2, 1);
  writeit::TableSlice two;
  CHECK(writeit::copy_table_rows(two_src.paragraphs, 1, 0, 1, two));
  CHECK(two.rows == 2 && two.columns == 1);
  const std::vector<writeit::Paragraph> full_before = full.paragraphs;
  CHECK(writeit::paste_table_rows(full.paragraphs, 1, two) == writeit::TablePaste::TooBig);
  CHECK(full.paragraphs == full_before);
  writeit::Document room = make_table(30, 1);
  writeit::TableSlice one;
  CHECK(writeit::copy_table_rows(full.paragraphs, 1, 0, 0, one));
  CHECK(writeit::paste_table_rows(room.paragraphs, 1, one) == writeit::TablePaste::Placed);
  CHECK(table_rows(room.paragraphs) == 31);

  writeit::Document columns = make_table(2, 2);
  writeit::apply_table_widths(columns.paragraphs, 1, {800, 1200});
  paint(columns.paragraphs[1], "A", false);
  paint(columns.paragraphs[2], "B", true);
  columns.paragraphs[2].cell.shading.pattern = 5000;
  columns.paragraphs[2].cell.shading.background = writeit::Ink::Red;
  paint(columns.paragraphs[3], "C", false);
  paint(columns.paragraphs[4], "D", false);
  writeit::TableSlice col;
  CHECK(writeit::copy_table_columns(columns.paragraphs, 2, 1, 1, col));
  CHECK(col.band == writeit::TableBand::Columns);
  CHECK(col.rows == 2 && col.columns == 1);
  CHECK(col.widths.size() == 1 && col.widths[0] == 1200);
  CHECK(text_of(col.paragraphs[0]) == "B" && text_of(col.paragraphs[1]) == "D");
  CHECK(writeit::paste_table_columns(columns.paragraphs, 1, col, 20000) ==
        writeit::TablePaste::Placed);
  CHECK(table_columns(columns.paragraphs) == 3);
  CHECK(cell_paragraph(columns.paragraphs, 0, 1) &&
        text_of(*cell_paragraph(columns.paragraphs, 0, 1)) == "B");
  CHECK(cell_paragraph(columns.paragraphs, 0, 1)->cell.shading.background == writeit::Ink::Red);
  CHECK(cell_paragraph(columns.paragraphs, 0, 2) &&
        text_of(*cell_paragraph(columns.paragraphs, 0, 2)) == "B");
  CHECK(cell_paragraph(columns.paragraphs, 1, 1) &&
        text_of(*cell_paragraph(columns.paragraphs, 1, 1)) == "D");
  CHECK(cell_paragraph(columns.paragraphs, 0, 0)->cell.widths.size() == 3);
  CHECK(cell_paragraph(columns.paragraphs, 0, 0)->cell.widths[1] == 1200);

  writeit::Document short_rows = make_table(1, 2);
  const std::vector<writeit::Paragraph> short_before = short_rows.paragraphs;
  CHECK(writeit::paste_table_columns(short_rows.paragraphs, 1, col, 20000) ==
        writeit::TablePaste::Misfit);
  CHECK(short_rows.paragraphs == short_before);

  const std::vector<writeit::Paragraph> columns_before = columns.paragraphs;
  CHECK(writeit::paste_table_rows(columns.paragraphs, 1, col) == writeit::TablePaste::Misfit);
  CHECK(columns.paragraphs == columns_before);
  CHECK(writeit::paste_table_columns(columns.paragraphs, 0, col, 20000) ==
        writeit::TablePaste::NotTable);

  writeit::Document pair_src = make_table(1, 2);
  writeit::TableSlice pair;
  CHECK(writeit::copy_table_columns(pair_src.paragraphs, 1, 0, 1, pair));
  CHECK(pair.rows == 1 && pair.columns == 2);
  writeit::Document many = make_table(1, 15);
  const std::vector<writeit::Paragraph> many_before = many.paragraphs;
  CHECK(writeit::paste_table_columns(many.paragraphs, 1, pair, 20000) == writeit::TablePaste::TooBig);
  CHECK(many.paragraphs == many_before);

  // 800 + 1200, plus the copied 1200, is 3200. A 1000 twip limit scales that
  // to 250, 375, and 375.
  writeit::Document scale = make_table(2, 2);
  writeit::apply_table_widths(scale.paragraphs, 1, {800, 1200});
  writeit::TableSlice wide_col;
  CHECK(writeit::copy_table_columns(scale.paragraphs, 2, 1, 1, wide_col));
  CHECK(writeit::paste_table_columns(scale.paragraphs, 1, wide_col, 1000) ==
        writeit::TablePaste::Placed);
  const writeit::Paragraph* scaled = cell_paragraph(scale.paragraphs, 0, 0);
  CHECK(scaled && scaled->cell.widths.size() == 3);
  CHECK(scaled && scaled->cell.widths[0] == 250 && scaled->cell.widths[1] == 375 &&
        scaled->cell.widths[2] == 375);

  writeit::Document hole = make_table(1, 2);
  hole.paragraphs.erase(hole.paragraphs.begin() + 2);
  writeit::TableSlice hole_slice;
  CHECK(writeit::copy_table_columns(hole.paragraphs, 1, 0, 1, hole_slice));
  CHECK(hole_slice.paragraphs.size() == 2);
  CHECK(text_of(hole_slice.paragraphs[1]).empty());

  std::vector<writeit::Paragraph> body = writeit::table_slice_paragraphs(rows);
  CHECK(body.size() == 3);
  CHECK(text_of(body[0]) == "C" && body[0].runs[0].bold && body[0].indents.left == 720);
  CHECK(body[0].cell.table == 0);
  CHECK(body[0].cell.shading.pattern == 0);
  CHECK(text_of(body[1]) == "C2" && text_of(body[2]) == "D");

  return suite_test::done("rows", kChecks);
}
