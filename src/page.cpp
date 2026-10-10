/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace writeit {
namespace {

int clamp_edge(int value)
{
  return std::max(kMinPaperTwips, std::min(kMaxPaperTwips, value));
}

// `margin` and the margin already accepted on the other side leave a quarter
// inch of the sheet.
int clamp_margin(int margin, int paper, int other)
{
  const int room = std::max(0, paper - kMinTextTwips);
  margin = std::max(0, std::min(margin, room));
  const int taken = std::max(0, std::min(other, room));
  if (margin + taken > room)
    margin = std::max(0, room - taken);
  return margin;
}

int next_table_id(const std::vector<Paragraph>& paragraphs)
{
  int id = 0;
  for (const Paragraph& paragraph : paragraphs)
    id = std::max(id, paragraph.cell.table);
  return id + 1;
}

void stamp_table(std::vector<Paragraph>& paragraphs, int id)
{
  int max_row = -1;
  int max_column = -1;
  std::vector<int> widths;
  for (const Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id)
      continue;
    max_row = std::max(max_row, paragraph.cell.row);
    max_column = std::max(max_column, paragraph.cell.column);
    if (widths.empty() && !paragraph.cell.widths.empty())
      widths = paragraph.cell.widths;
  }
  if (max_row < 0)
    return;
  const int columns = max_column + 1;
  if (static_cast<int>(widths.size()) > columns)
    widths.resize(static_cast<size_t>(columns));
  while (static_cast<int>(widths.size()) < columns)
    widths.push_back(kMinCellTwips);
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id)
      continue;
    paragraph.cell.rows = max_row + 1;
    paragraph.cell.columns = columns;
    paragraph.cell.widths = widths;
  }
}

// The last paragraph of this row and column, or the paragraph just before
// the row when the column is missing.
size_t last_in_cell(const std::vector<Paragraph>& paragraphs, int id, int row, int column)
{
  size_t found = paragraphs.size();
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const Cell& cell = paragraphs[i].cell;
    if (cell.table == id && cell.row == row && cell.column == column)
      found = i;
  }
  return found;
}

Paragraph empty_cell(const Cell& like, int row, int column)
{
  Paragraph paragraph;
  paragraph.cell = like;
  paragraph.cell.row = row;
  paragraph.cell.column = column;
  return paragraph;
}

}  // namespace

std::string image_type_of(const std::string& data)
{
  const auto at = [&](size_t i) { return static_cast<unsigned char>(data[i]); };
  if (data.size() >= 8 && at(0) == 0x89 && at(1) == 'P' && at(2) == 'N' && at(3) == 'G')
    return "png";
  if (data.size() >= 3 && at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF)
    return "jpeg";
  return "";
}

bool load_image_file(const std::string& path, Image& image)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0 || static_cast<std::size_t>(size) > kMaxImageBytes)
    return false;
  in.seekg(0, std::ios::beg);
  std::string bytes(static_cast<size_t>(size), '\0');
  in.read(bytes.data(), size);
  if (!in)
    return false;
  const std::string type = image_type_of(bytes);
  if (type.empty())
    return false;
  image.data = std::move(bytes);
  image.type = type;
  return true;
}

bool operator==(const PageSetup& a, const PageSetup& b)
{
  return a.paper_width == b.paper_width && a.paper_height == b.paper_height &&
         a.margin_left == b.margin_left && a.margin_right == b.margin_right &&
         a.margin_top == b.margin_top && a.margin_bottom == b.margin_bottom &&
         a.columns == b.columns && a.column_gap == b.column_gap && a.landscape == b.landscape;
}

bool operator!=(const PageSetup& a, const PageSetup& b)
{
  return !(a == b);
}

bool operator==(const Cell& a, const Cell& b)
{
  return a.table == b.table && a.row == b.row && a.column == b.column && a.rows == b.rows &&
         a.columns == b.columns && a.widths == b.widths;
}

bool operator!=(const Cell& a, const Cell& b)
{
  return !(a == b);
}

PageSetup clamp_page(PageSetup page)
{
  page.paper_width = clamp_edge(page.paper_width);
  page.paper_height = clamp_edge(page.paper_height);
  page.margin_left = clamp_margin(page.margin_left, page.paper_width, 0);
  page.margin_right = clamp_margin(page.margin_right, page.paper_width, page.margin_left);
  page.margin_top = clamp_margin(page.margin_top, page.paper_height, 0);
  page.margin_bottom = clamp_margin(page.margin_bottom, page.paper_height, page.margin_top);
  page.columns = std::max(1, std::min(kMaxColumns, page.columns));
  page.column_gap = std::max(0, std::min(kMaxColumnGap, page.column_gap));
  if (page.columns == 1)
    page.column_gap = default_page().column_gap;
  return page;
}

bool page_metrics_default(const PageSetup& page)
{
  const PageSetup plain = default_page();
  return page.paper_width == plain.paper_width && page.paper_height == plain.paper_height &&
         page.margin_left == plain.margin_left && page.margin_right == plain.margin_right &&
         page.margin_top == plain.margin_top && page.margin_bottom == plain.margin_bottom &&
         page.landscape == plain.landscape;
}

int page_text_twips(const PageSetup& page)
{
  const PageSetup clamped = clamp_page(page);
  return std::max(kMinTextTwips, clamped.paper_width - clamped.margin_left - clamped.margin_right);
}

bool insert_table(std::vector<Paragraph>& paragraphs, size_t at, int rows, int columns,
                  int text_width)
{
  if (rows < 1 || columns < 1 || rows > kMaxTableRows || columns > kMaxTableColumns)
    return false;
  if (paragraphs.empty())
    paragraphs.emplace_back();
  if (at > paragraphs.size())
    at = paragraphs.size();
  const int width = std::max(kMinCellTwips * columns, text_width);
  std::vector<int> widths(static_cast<size_t>(columns), std::max(kMinCellTwips, width / columns));
  int used = 0;
  for (int i = 0; i + 1 < columns; ++i)
    used += widths[static_cast<size_t>(i)];
  widths.back() = std::max(kMinCellTwips, width - used);
  Cell cell;
  cell.table = next_table_id(paragraphs);
  cell.rows = rows;
  cell.columns = columns;
  cell.widths = widths;
  std::vector<Paragraph> cells;
  cells.reserve(static_cast<size_t>(rows * columns));
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column)
      cells.push_back(empty_cell(cell, row, column));
  }
  paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at), cells.begin(),
                    cells.end());
  return true;
}

bool in_table(const std::vector<Paragraph>& paragraphs, size_t index)
{
  return index < paragraphs.size() && paragraphs[index].cell.table != 0;
}

bool insert_table_row(std::vector<Paragraph>& paragraphs, size_t index)
{
  if (!in_table(paragraphs, index))
    return false;
  const Cell cell = paragraphs[index].cell;
  if (cell.rows >= kMaxTableRows)
    return false;
  const int row = cell.row + 1;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table == cell.table && paragraph.cell.row >= row)
      ++paragraph.cell.row;
  }
  size_t insert_at = 0;
  bool found = false;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const Cell& other = paragraphs[i].cell;
    if (other.table == cell.table && other.row == cell.row) {
      insert_at = i + 1;
      found = true;
    }
  }
  if (!found)
    return false;
  for (int column = cell.columns - 1; column >= 0; --column) {
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(insert_at),
                      empty_cell(cell, row, column));
  }
  stamp_table(paragraphs, cell.table);
  return true;
}

bool delete_table_row(std::vector<Paragraph>& paragraphs, size_t index)
{
  if (!in_table(paragraphs, index))
    return false;
  const Cell cell = paragraphs[index].cell;
  const int id = cell.table;
  const int row = cell.row;
  paragraphs.erase(std::remove_if(paragraphs.begin(), paragraphs.end(),
                                  [&](const Paragraph& paragraph) {
                                    return paragraph.cell.table == id && paragraph.cell.row == row;
                                  }),
                   paragraphs.end());
  bool any = false;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id)
      continue;
    any = true;
    if (paragraph.cell.row > row)
      --paragraph.cell.row;
  }
  if (any)
    stamp_table(paragraphs, id);
  if (paragraphs.empty())
    paragraphs.emplace_back();
  return true;
}

bool insert_table_column(std::vector<Paragraph>& paragraphs, size_t index)
{
  if (!in_table(paragraphs, index))
    return false;
  const Cell cell = paragraphs[index].cell;
  if (cell.columns >= kMaxTableColumns)
    return false;
  const int id = cell.table;
  const int column = cell.column;
  int rows = 0;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id)
      continue;
    rows = std::max(rows, paragraph.cell.row + 1);
    if (paragraph.cell.column > column)
      ++paragraph.cell.column;
  }
  for (int row = rows - 1; row >= 0; --row) {
    size_t at = last_in_cell(paragraphs, id, row, column);
    if (at == paragraphs.size())
      continue;
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at) + 1,
                      empty_cell(cell, row, column + 1));
  }
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id || paragraph.cell.widths.empty())
      continue;
    auto& widths = paragraph.cell.widths;
    const size_t at = static_cast<size_t>(std::min(column, static_cast<int>(widths.size()) - 1));
    const int half = std::max(kMinCellTwips, widths[at] / 2);
    widths[at] = std::max(kMinCellTwips, widths[at] - half);
    widths.insert(widths.begin() + static_cast<std::ptrdiff_t>(at) + 1, half);
    break;
  }
  stamp_table(paragraphs, id);
  return true;
}

bool delete_table_column(std::vector<Paragraph>& paragraphs, size_t index)
{
  if (!in_table(paragraphs, index))
    return false;
  const int id = paragraphs[index].cell.table;
  const int column = paragraphs[index].cell.column;
  paragraphs.erase(std::remove_if(paragraphs.begin(), paragraphs.end(),
                                  [&](const Paragraph& paragraph) {
                                    return paragraph.cell.table == id &&
                                           paragraph.cell.column == column;
                                  }),
                   paragraphs.end());
  bool any = false;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id)
      continue;
    any = true;
    if (paragraph.cell.column > column)
      --paragraph.cell.column;
    if (static_cast<int>(paragraph.cell.widths.size()) > column)
      paragraph.cell.widths.erase(paragraph.cell.widths.begin() + column);
  }
  if (any)
    stamp_table(paragraphs, id);
  if (paragraphs.empty())
    paragraphs.emplace_back();
  return true;
}

}  // namespace writeit
