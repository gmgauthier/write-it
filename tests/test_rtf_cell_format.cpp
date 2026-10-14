/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 9;

writeit::Paragraph cell_paragraph()
{
  writeit::Paragraph paragraph;
  paragraph.cell.table = 1;
  paragraph.cell.row = 0;
  paragraph.cell.column = 0;
  paragraph.cell.rows = 1;
  paragraph.cell.columns = 1;
  paragraph.cell.widths = {5000};
  return paragraph;
}

int main()
{
  writeit::Document centered = writeit::blank_document("Sans", 11);
  writeit::Paragraph paragraph = cell_paragraph();
  paragraph.align = writeit::Align::Center;
  writeit::Run run;
  run.text = "A";
  run.font = "Sans";
  run.size = 20;
  paragraph.runs.push_back(run);
  centered.paragraphs = {paragraph};
  const std::string rtf = writeit::rtf_export(centered);
  CHECK(rtf.find("\\pard\\intbl\\qc") != std::string::npos);
  CHECK(rtf.find("\\fs40") != std::string::npos);
  writeit::Document back;
  CHECK(writeit::rtf_import(rtf, back));
  CHECK(back.paragraphs.size() == 1);
  CHECK(back.paragraphs[0].align == writeit::Align::Center);
  CHECK(back.paragraphs[0].runs.size() == 1 && back.paragraphs[0].runs[0].text == "A");
  CHECK(back.paragraphs[0].runs[0].size == 20);

  writeit::Document empty = writeit::blank_document("Sans", 11);
  writeit::Paragraph blank = cell_paragraph();
  writeit::Run mark;
  mark.font = "Sans";
  mark.size = 20;
  blank.mark = mark;
  empty.paragraphs = {blank};
  writeit::Document empty_back;
  CHECK(writeit::rtf_import(writeit::rtf_export(empty), empty_back));
  CHECK(empty_back.paragraphs.size() == 1 && empty_back.paragraphs[0].mark &&
        empty_back.paragraphs[0].mark->size == 20);

  return suite_test::done("rtf-cell-format", kChecks);
}
