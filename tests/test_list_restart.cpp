/* SPDX-License-Identifier: Unlicense */

// Restart Numbering writes the new list id at the body offset of the item.
// A table between two items is one anchor. Walking paragraph_end() once per
// cell lands the id on the wrong paragraph.

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 8;

writeit::Paragraph numbered(const std::string& text)
{
  writeit::Paragraph paragraph;
  paragraph.list.kind = writeit::ListKind::Number;
  paragraph.list.list = 1;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  return paragraph;
}

writeit::Paragraph cell(int column)
{
  writeit::Paragraph paragraph;
  paragraph.cell.table = 1;
  paragraph.cell.row = 0;
  paragraph.cell.column = column;
  paragraph.cell.rows = 1;
  paragraph.cell.columns = 2;
  writeit::Run run;
  run.text = "C";
  paragraph.runs.push_back(run);
  return paragraph;
}

int main()
{
  std::vector<writeit::Paragraph> before = {numbered("One"), cell(0), cell(1), numbered("Two")};
  CHECK(writeit::chars_before(before, 3) == 6);
  std::vector<writeit::Paragraph> after = before;
  CHECK(writeit::restart_numbering(after, 3));
  CHECK(after[0].list.list == 1);
  CHECK(after[1].list.kind == writeit::ListKind::None);
  CHECK(after[2].list.kind == writeit::ListKind::None);
  CHECK(after[3].list.kind == writeit::ListKind::Number);
  CHECK(after[3].list.list != before[3].list.list);
  const std::vector<writeit::ListRetag> tags = writeit::list_retag_offsets(before, after);
  CHECK(tags.size() == 1 && tags[0].offset == 6 && tags[0].index == 3);
  return suite_test::done("list-restart", kChecks);
}
