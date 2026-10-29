/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"

#include <gdkmm/pixbuf.h>

#include <glibmm/ustring.h>

#include <algorithm>
#include <fstream>
#include <limits>
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

// The cell already in this row and column, so a new neighbour keeps that
// cell's borders and shading. A missing cell keeps the table's geometry and
// a plain frame.
Cell chrome_from(const std::vector<Paragraph>& paragraphs, int id, int row, int column,
                 const Cell& geometry)
{
  for (const Paragraph& paragraph : paragraphs) {
    const Cell& other = paragraph.cell;
    if (other.table == id && other.row == row && other.column == column)
      return other;
  }
  Cell plain = geometry;
  plain.borders_set = false;
  plain.borders = Borders{};
  plain.shading = Shading{};
  return plain;
}

int column_width_at(const Cell& cell, int column)
{
  if (column >= 0 && column < static_cast<int>(cell.widths.size())) {
    const int width = cell.widths[static_cast<size_t>(column)];
    if (width > 0)
      return width;
  }
  return kMinCellTwips;
}

// Selected cells, renumbered so the band starts at row 0, column 0. A missing
// cell is one empty paragraph, so a paste still has a place for it.
bool copy_band(const std::vector<Paragraph>& paragraphs, size_t index, int row0, int row1,
               int column0, int column1, TableBand band, TableSlice& out)
{
  if (!in_table(paragraphs, index))
    return false;
  if (row0 > row1)
    std::swap(row0, row1);
  if (column0 > column1)
    std::swap(column0, column1);
  const Cell cell = paragraphs[index].cell;
  if (cell.rows < 1 || cell.columns < 1)
    return false;
  if (row0 < 0 || column0 < 0 || row1 >= cell.rows || column1 >= cell.columns)
    return false;
  TableSlice slice;
  slice.band = band;
  slice.rows = row1 - row0 + 1;
  slice.columns = column1 - column0 + 1;
  slice.widths.reserve(static_cast<size_t>(slice.columns));
  for (int column = column0; column <= column1; ++column)
    slice.widths.push_back(column_width_at(cell, column));
  for (int row = row0; row <= row1; ++row) {
    for (int column = column0; column <= column1; ++column) {
      bool any = false;
      for (const Paragraph& paragraph : paragraphs) {
        if (paragraph.cell.table != cell.table || paragraph.cell.row != row ||
            paragraph.cell.column != column)
          continue;
        Paragraph copy = paragraph;
        copy.cell.table = 0;
        copy.cell.row = row - row0;
        copy.cell.column = column - column0;
        copy.cell.rows = slice.rows;
        copy.cell.columns = slice.columns;
        copy.cell.widths = slice.widths;
        slice.paragraphs.push_back(std::move(copy));
        any = true;
      }
      if (!any) {
        Paragraph blank;
        blank.cell.row = row - row0;
        blank.cell.column = column - column0;
        blank.cell.rows = slice.rows;
        blank.cell.columns = slice.columns;
        blank.cell.widths = slice.widths;
        slice.paragraphs.push_back(std::move(blank));
      }
    }
  }
  out = std::move(slice);
  return true;
}

bool paragraph_is_cell(const std::vector<Paragraph>& paragraphs, size_t index)
{
  return index < paragraphs.size() && paragraphs[index].cell.table != 0;
}

int clamped_into(const Paragraph& paragraph, int into)
{
  const int length = chars_of(paragraph);
  if (into < 0)
    return 0;
  if (into > length)
    return length;
  return into;
}

// Same cut as a page break, without forcing one. The right half is the rest
// of the paragraph, not the start of a sheet. A mark stays on the left.
void divide_paragraph(Paragraph& left, int at, Paragraph& right)
{
  right = left;
  right.runs.clear();
  right.page_break = false;
  right.section_break = SectionBreak::None;
  right.mark.reset();
  if (at < 0)
    at = 0;
  std::vector<Run> kept;
  bool splitting = true;
  int seen = 0;
  for (const Run& run : left.runs) {
    const int n = chars_of(run);
    if (!splitting) {
      right.runs.push_back(run);
      continue;
    }
    if (seen + n <= at) {
      kept.push_back(run);
      seen += n;
      continue;
    }
    const int into = at - seen;
    if (run.image) {
      if (into <= 0)
        right.runs.push_back(run);
      else
        kept.push_back(run);
    } else {
      const Glib::ustring text(run.text);
      if (into > 0) {
        Run part = run;
        part.text = text.substr(0, static_cast<Glib::ustring::size_type>(into)).raw();
        kept.push_back(part);
      }
      if (into < static_cast<int>(text.length())) {
        Run part = run;
        part.text = text.substr(static_cast<Glib::ustring::size_type>(into)).raw();
        if (into > 0)
          part.note = 0;
        part.image.reset();
        right.runs.push_back(part);
      }
    }
    splitting = false;
  }
  left.runs = std::move(kept);
}

bool selection_touches_table(const std::vector<Paragraph>& paragraphs, size_t from_at,
                             int from_into, size_t to_at, int to_into)
{
  if (from_at == to_at && from_into == to_into)
    return false;
  if (paragraph_is_cell(paragraphs, from_at))
    return true;
  if (to_into > 0 && paragraph_is_cell(paragraphs, to_at))
    return true;
  const size_t end = std::min(to_at, paragraphs.size());
  for (size_t i = from_at + 1; i < end; ++i) {
    if (paragraph_is_cell(paragraphs, i))
      return true;
  }
  return false;
}

int place_collapsed(std::vector<Paragraph>& paragraphs, size_t at, int into,
                    const std::vector<Paragraph>& block)
{
  const bool blank = paragraphs.size() == 1 && paragraphs[0].cell.table == 0 &&
                     paragraphs[0].runs.empty() && !paragraphs[0].page_break;
  if (blank || paragraphs.empty()) {
    paragraphs = block;
    return chars_before(paragraphs, paragraphs.size());
  }
  if (at > paragraphs.size())
    at = paragraphs.size();
  if (at == paragraphs.size()) {
    paragraphs.insert(paragraphs.end(), block.begin(), block.end());
    return chars_before(paragraphs, paragraphs.size());
  }
  if (paragraphs[at].cell.table != 0) {
    const int id = paragraphs[at].cell.table;
    while (at > 0 && paragraphs[at - 1].cell.table == id)
      --at;
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at), block.begin(),
                      block.end());
    return chars_before(paragraphs, at + block.size());
  }
  into = clamped_into(paragraphs[at], into);
  const int length = chars_of(paragraphs[at]);
  if (into <= 0) {
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at), block.begin(),
                      block.end());
    return chars_before(paragraphs, at + block.size());
  }
  if (into >= length) {
    const size_t dest = at + 1;
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(dest), block.begin(),
                      block.end());
    return chars_before(paragraphs, dest + block.size());
  }
  Paragraph right;
  divide_paragraph(paragraphs[at], into, right);
  paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at + 1), std::move(right));
  paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at + 1), block.begin(),
                    block.end());
  return chars_before(paragraphs, at + 1 + block.size());
}

int replace_body(std::vector<Paragraph>& paragraphs, size_t from_at, int from_into, size_t to_at,
                 int to_into, const std::vector<Paragraph>& block)
{
  if (from_at > paragraphs.size())
    from_at = paragraphs.size();
  std::vector<Paragraph> out;
  out.reserve(paragraphs.size() + block.size());
  out.insert(out.end(), paragraphs.begin(),
             paragraphs.begin() + static_cast<std::ptrdiff_t>(from_at));

  bool inherit_break = false;
  SectionBreak inherit_section = SectionBreak::None;
  if (from_at < paragraphs.size() && paragraphs[from_at].cell.table == 0) {
    const int cut = clamped_into(paragraphs[from_at], from_into);
    if (cut > 0) {
      Paragraph left = paragraphs[from_at];
      Paragraph discarded;
      divide_paragraph(left, cut, discarded);
      out.push_back(std::move(left));
    } else {
      inherit_break = paragraphs[from_at].page_break;
      inherit_section = paragraphs[from_at].section_break;
    }
  }

  std::vector<Paragraph> placed = block;
  if (inherit_break && !placed.empty())
    placed[0].page_break = true;
  if (inherit_section != SectionBreak::None && !placed.empty() && placed[0].cell.table == 0 &&
      placed[0].section_break == SectionBreak::None)
    placed[0].section_break = inherit_section;
  const size_t follow = out.size() + placed.size();
  out.insert(out.end(), placed.begin(), placed.end());

  size_t resume = to_at;
  if (to_at < paragraphs.size() && paragraphs[to_at].cell.table == 0) {
    const int end_into = clamped_into(paragraphs[to_at], to_into);
    const int length = chars_of(paragraphs[to_at]);
    if (end_into <= 0)
      resume = to_at;
    else if (end_into >= length)
      resume = to_at + 1;
    else {
      Paragraph source = paragraphs[to_at];
      Paragraph suffix;
      divide_paragraph(source, end_into, suffix);
      if (chars_of(suffix) > 0)
        out.push_back(std::move(suffix));
      resume = to_at + 1;
    }
  } else if (to_at > paragraphs.size()) {
    resume = paragraphs.size();
  }
  if (resume < paragraphs.size())
    out.insert(out.end(), paragraphs.begin() + static_cast<std::ptrdiff_t>(resume),
               paragraphs.end());
  paragraphs = std::move(out);
  if (follow > paragraphs.size())
    return chars_before(paragraphs, paragraphs.size());
  return chars_before(paragraphs, follow);
}

Paragraph placed_cell(Paragraph paragraph, int table, int row, int column, int rows, int columns,
                      const std::vector<int>& widths)
{
  paragraph.cell.table = table;
  paragraph.cell.row = row;
  paragraph.cell.column = column;
  paragraph.cell.rows = rows;
  paragraph.cell.columns = columns;
  paragraph.cell.widths = widths;
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

Image image_from_pixbuf(const Glib::RefPtr<Gdk::Pixbuf>& pix)
{
  Image image;
  if (!pix)
    return image;
  gchar* raw = nullptr;
  gsize size = 0;
  try {
    pix->save_to_buffer(raw, size, "png");
  } catch (const Glib::Error&) {
    g_free(raw);
    return image;
  }
  if (raw != nullptr && size > 0 && size <= kMaxImageBytes)
    image.data.assign(raw, raw + size);
  g_free(raw);
  if (image_type_of(image.data) != "png")
    return {};
  image.type = "png";
  image.alt = "image";
  return image;
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
         a.columns == b.columns && a.widths == b.widths && a.borders_set == b.borders_set &&
         a.borders == b.borders && a.shading == b.shading;
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

int table_width_limit(const PageSetup& page)
{
  const PageSetup clamped = clamp_page(page);
  const int text = page_text_twips(clamped);
  const int through_right = clamped.paper_width - clamped.margin_left;
  return std::max(text, through_right);
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
    const Cell source = chrome_from(paragraphs, cell.table, cell.row, column, cell);
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(insert_at),
                      empty_cell(source, row, column));
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
                      empty_cell(paragraphs[at].cell, row, column + 1));
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

bool copy_table_rows(const std::vector<Paragraph>& paragraphs, size_t index, int first, int last,
                     TableSlice& out)
{
  if (!in_table(paragraphs, index))
    return false;
  const Cell cell = paragraphs[index].cell;
  return copy_band(paragraphs, index, first, last, 0, cell.columns - 1, TableBand::Rows, out);
}

bool copy_table_columns(const std::vector<Paragraph>& paragraphs, size_t index, int first, int last,
                        TableSlice& out)
{
  if (!in_table(paragraphs, index))
    return false;
  const Cell cell = paragraphs[index].cell;
  return copy_band(paragraphs, index, 0, cell.rows - 1, first, last, TableBand::Columns, out);
}

bool paint_table_band(std::vector<Paragraph>& paragraphs, size_t index, TableBand band, int first,
                      int last, const Shading& shading)
{
  if (!in_table(paragraphs, index))
    return false;
  if (first > last)
    std::swap(first, last);
  const Cell cell = paragraphs[index].cell;
  if (cell.rows < 1 || cell.columns < 1)
    return false;
  int row0 = 0;
  int row1 = cell.rows - 1;
  int column0 = 0;
  int column1 = cell.columns - 1;
  if (band == TableBand::Rows) {
    row0 = first;
    row1 = last;
  } else {
    column0 = first;
    column1 = last;
  }
  if (row0 < 0 || column0 < 0 || row1 >= cell.rows || column1 >= cell.columns)
    return false;
  const Shading paint = clamp_shading(shading);
  const int id = cell.table;
  bool any = false;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id || paragraph.cell.row < row0 || paragraph.cell.row > row1 ||
        paragraph.cell.column < column0 || paragraph.cell.column > column1)
      continue;
    paragraph.cell.shading = paint;
    any = true;
  }
  return any;
}

namespace {

constexpr unsigned kCharacterDirect = kDirectFont | kDirectSize | kDirectBold | kDirectItalic |
                                      kDirectUnderline | kDirectColor | kDirectHighlight |
                                      kDirectStrike | kDirectScript;

Run bare_character(Run run)
{
  run.text.clear();
  run.note = 0;
  run.image.reset();
  run.direct &= kCharacterDirect;
  return run;
}

Run character_at(const Paragraph& paragraph, int into)
{
  const int count = chars_of(paragraph);
  if (count <= 0) {
    if (paragraph.mark)
      return bare_character(*paragraph.mark);
    return bare_character(Run{});
  }
  if (into < 0)
    into = 0;
  if (into >= count)
    into = count - 1;
  int at = 0;
  const Run* found = nullptr;
  for (const Run& run : paragraph.runs) {
    const int n = chars_of(run);
    if (n <= 0)
      continue;
    if (into < at + n) {
      found = &run;
      break;
    }
    at += n;
    found = &run;
  }
  if (!found)
    return bare_character(Run{});
  return bare_character(*found);
}

void take_paragraph_format(Paragraph& dest, const Paragraph& src)
{
  dest.heading = src.heading;
  dest.style = src.style.empty() ? std::string(kNormalStyle) : src.style;
  dest.indents = clamp_indents(src.indents);
  dest.align = src.align;
  dest.spacing = clamp_spacing(src.spacing);
  dest.borders = clamp_borders(src.borders);
  dest.shading = clamp_shading(src.shading);
  dest.tabs = clamp_tabs(src.tabs);
  dest.list = clamp_list(src.list);
  dest.direct = src.direct;
}

void apply_character(Run& run, const Run& format)
{
  run.font = format.font;
  run.size = format.size;
  run.bold = format.bold;
  run.italic = format.italic;
  run.underline = format.underline;
  run.color = format.color;
  run.highlight = format.highlight;
  run.strike = format.strike;
  run.script = format.script;
  run.direct = (run.direct & ~kCharacterDirect) | (format.direct & kCharacterDirect);
}

bool paintable_run(const Run& run)
{
  return !run.image && run.note == 0 && !run.text.empty();
}

void paint_characters(Paragraph& paragraph, int from, int to, const Run& format)
{
  if (from > to)
    std::swap(from, to);
  if (chars_of(paragraph) <= 0) {
    paragraph.mark = bare_character(format);
    paragraph.runs.clear();
    return;
  }
  if (from >= to)
    return;
  if (from < 0)
    from = 0;
  std::vector<Run> out;
  int at = 0;
  for (const Run& run : paragraph.runs) {
    const int n = chars_of(run);
    if (n <= 0 || !paintable_run(run)) {
      out.push_back(run);
      at += n;
      continue;
    }
    const int run_end = at + n;
    if (run_end <= from || at >= to) {
      out.push_back(run);
      at = run_end;
      continue;
    }
    const int left_n = std::max(0, from - at);
    const int right_n = std::max(0, run_end - to);
    const int mid_n = n - left_n - right_n;
    const Glib::ustring text(run.text);
    auto piece = [&](int start, int len, bool paint) {
      if (len <= 0)
        return;
      Run part = run;
      part.text = text.substr(static_cast<size_t>(start), static_cast<size_t>(len)).raw();
      if (paint)
        apply_character(part, format);
      out.push_back(std::move(part));
    };
    piece(0, left_n, false);
    piece(left_n, mid_n, true);
    piece(left_n + mid_n, right_n, false);
    at = run_end;
  }
  std::vector<Run> merged;
  for (Run& run : out) {
    if (!merged.empty() && paintable_run(run) && paintable_run(merged.back()) &&
        same_format(merged.back(), run))
      merged.back().text += run.text;
    else
      merged.push_back(std::move(run));
  }
  paragraph.runs = std::move(merged);
}

void push_paint_span(std::vector<PaintSpan>& spans, size_t index, int start, int chars,
                     int boundary, bool last, int from, int to)
{
  const bool overlaps = from < boundary && to > start;
  const int caret_limit = last ? boundary : boundary - 1;
  const bool caret_here = from == to && from >= start && from <= caret_limit;
  if (!overlaps && !caret_here)
    return;
  int rel_from = 0;
  int rel_to = 0;
  if (to > from) {
    const int lo = std::max(start, from);
    const int hi = std::min(start + chars, to);
    if (hi > lo) {
      rel_from = lo - start;
      rel_to = hi - start;
    }
  }
  spans.push_back(PaintSpan{index, rel_from, rel_to});
}

}  // namespace

bool sample_painter(const std::vector<Paragraph>& paragraphs, size_t index, int into,
                    PainterFormat& out)
{
  if (index >= paragraphs.size())
    return false;
  const Paragraph& paragraph = paragraphs[index];
  PainterFormat format;
  format.character = character_at(paragraph, into);
  format.paragraph = paragraph;
  format.paragraph.runs.clear();
  format.paragraph.mark.reset();
  format.paragraph.basis = 0;
  format.from_cell = paragraph.cell.table != 0;
  format.cell_shading = paragraph.cell.shading;
  out = std::move(format);
  return true;
}

bool paint_spans(std::vector<Paragraph>& paragraphs, const std::vector<PaintSpan>& spans,
                 const PainterFormat& format)
{
  if (spans.empty())
    return false;
  for (const PaintSpan& span : spans) {
    if (span.index >= paragraphs.size())
      return false;
  }
  std::vector<Paragraph> painted = paragraphs;
  for (PaintSpan span : spans) {
    Paragraph& paragraph = painted[span.index];
    take_paragraph_format(paragraph, format.paragraph);
    paint_characters(paragraph, span.from, span.to, format.character);
  }
  if (format.from_cell) {
    const Shading shading = clamp_shading(format.cell_shading);
    for (const PaintSpan& span : spans) {
      const Cell& cell = painted[span.index].cell;
      if (cell.table == 0)
        continue;
      for (Paragraph& paragraph : painted) {
        if (paragraph.cell.table == cell.table && paragraph.cell.row == cell.row &&
            paragraph.cell.column == cell.column)
          paragraph.cell.shading = shading;
      }
    }
  }
  if (same_stored_paragraphs(painted, paragraphs))
    return false;
  paragraphs.swap(painted);
  return true;
}

bool painter_body_spans(const std::vector<Paragraph>& paragraphs, int from, int to,
                        std::vector<PaintSpan>& spans)
{
  spans.clear();
  if (paragraphs.empty())
    return false;
  if (from > to)
    std::swap(from, to);
  if (from < 0)
    from = 0;
  if (to < 0)
    to = 0;
  int cursor = 0;
  size_t i = 0;
  while (i < paragraphs.size()) {
    if (paragraphs[i].cell.table != 0) {
      const int id = paragraphs[i].cell.table;
      size_t j = i + 1;
      while (j < paragraphs.size() && paragraphs[j].cell.table == id)
        ++j;
      cursor += 1;
      if (j < paragraphs.size())
        cursor += 1;
      i = j;
      continue;
    }
    const int chars = chars_of(paragraphs[i]);
    const bool last = i + 1 == paragraphs.size();
    const int boundary = cursor + chars + (last ? 0 : 1);
    push_paint_span(spans, i, cursor, chars, boundary, last, from, to);
    cursor = boundary;
    ++i;
  }
  return !spans.empty();
}

bool painter_linear_spans(const std::vector<Paragraph>& paragraphs, size_t base, int from, int to,
                          std::vector<PaintSpan>& spans)
{
  spans.clear();
  if (paragraphs.empty())
    return false;
  if (from > to)
    std::swap(from, to);
  if (from < 0)
    from = 0;
  if (to < 0)
    to = 0;
  int cursor = 0;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const int chars = chars_of(paragraphs[i]);
    const bool last = i + 1 == paragraphs.size();
    const int boundary = cursor + chars + (last ? 0 : 1);
    push_paint_span(spans, base + i, cursor, chars, boundary, last, from, to);
    cursor = boundary;
  }
  return !spans.empty();
}

TablePaste paste_table_rows(std::vector<Paragraph>& paragraphs, size_t index,
                            const TableSlice& slice)
{
  if (!in_table(paragraphs, index))
    return TablePaste::NotTable;
  if (slice.band != TableBand::Rows || slice.rows < 1 || slice.columns < 1)
    return TablePaste::Misfit;
  const Cell cell = paragraphs[index].cell;
  if (slice.columns != cell.columns)
    return TablePaste::Misfit;
  if (cell.rows + slice.rows > kMaxTableRows)
    return TablePaste::TooBig;
  const int id = cell.table;
  const int at_row = cell.row;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table == id && paragraph.cell.row > at_row)
      paragraph.cell.row += slice.rows;
  }
  size_t insert_at = paragraphs.size();
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const Cell& other = paragraphs[i].cell;
    if (other.table == id && other.row == at_row)
      insert_at = i + 1;
  }
  const int rows = cell.rows + slice.rows;
  std::vector<Paragraph> block;
  block.reserve(slice.paragraphs.size());
  for (const Paragraph& paragraph : slice.paragraphs) {
    block.push_back(placed_cell(paragraph, id, at_row + 1 + paragraph.cell.row,
                                paragraph.cell.column, rows, cell.columns, cell.widths));
  }
  paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(insert_at), block.begin(),
                    block.end());
  stamp_table(paragraphs, id);
  return TablePaste::Placed;
}

TablePaste paste_table_columns(std::vector<Paragraph>& paragraphs, size_t index,
                               const TableSlice& slice, int width_limit)
{
  if (!in_table(paragraphs, index))
    return TablePaste::NotTable;
  if (slice.band != TableBand::Columns || slice.rows < 1 || slice.columns < 1)
    return TablePaste::Misfit;
  const Cell cell = paragraphs[index].cell;
  if (slice.rows != cell.rows)
    return TablePaste::Misfit;
  if (cell.columns + slice.columns > kMaxTableColumns)
    return TablePaste::TooBig;
  std::vector<int> widths = cell.widths;
  if (static_cast<int>(widths.size()) > cell.columns)
    widths.resize(static_cast<size_t>(cell.columns));
  while (static_cast<int>(widths.size()) < cell.columns)
    widths.push_back(kMinCellTwips);
  std::vector<int> extra = slice.widths;
  if (static_cast<int>(extra.size()) > slice.columns)
    extra.resize(static_cast<size_t>(slice.columns));
  while (static_cast<int>(extra.size()) < slice.columns)
    extra.push_back(kMinCellTwips);
  const int at = cell.column + 1;
  widths.insert(widths.begin() + at, extra.begin(), extra.end());
  long long sum = 0;
  for (int width : widths) {
    sum += std::max(0, width);
    if (sum > static_cast<long long>(std::numeric_limits<int>::max()))
      sum = std::numeric_limits<int>::max();
  }
  const int columns = cell.columns + slice.columns;
  const int floor_sum = kMinCellTwips * columns;
  int limit = std::max(width_limit, floor_sum);
  if (sum > limit && !fit_table_widths(widths, static_cast<int>(sum), limit))
    return TablePaste::Misfit;
  const int id = cell.table;
  const int at_column = cell.column;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table == id && paragraph.cell.column > at_column)
      paragraph.cell.column += slice.columns;
  }
  const int rows = cell.rows;
  for (int row = rows - 1; row >= 0; --row) {
    size_t insert_at = last_in_cell(paragraphs, id, row, at_column);
    if (insert_at == paragraphs.size())
      continue;
    std::vector<Paragraph> block;
    for (const Paragraph& paragraph : slice.paragraphs) {
      if (paragraph.cell.row != row)
        continue;
      block.push_back(placed_cell(paragraph, id, row, at_column + 1 + paragraph.cell.column, rows,
                                  columns, widths));
    }
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(insert_at) + 1,
                      block.begin(), block.end());
  }
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table == id)
      paragraph.cell.widths = widths;
  }
  stamp_table(paragraphs, id);
  return TablePaste::Placed;
}

std::vector<Paragraph> table_slice_paragraphs(const TableSlice& slice)
{
  std::vector<Paragraph> out;
  if (slice.rows < 1 || slice.columns < 1)
    return out;
  for (int row = 0; row < slice.rows; ++row) {
    for (int column = 0; column < slice.columns; ++column) {
      bool any = false;
      for (const Paragraph& paragraph : slice.paragraphs) {
        if (paragraph.cell.row != row || paragraph.cell.column != column)
          continue;
        Paragraph body = paragraph;
        body.cell = Cell{};
        body.page_break = false;
        body.section_break = SectionBreak::None;
        out.push_back(std::move(body));
        any = true;
      }
      if (!any)
        out.emplace_back();
    }
  }
  return out;
}

int insert_pasted_paragraphs(std::vector<Paragraph>& paragraphs, size_t at, int into,
                             size_t from_at, int from_into, size_t to_at, int to_into,
                             const std::vector<Paragraph>& block)
{
  if (block.empty())
    return -1;
  if (from_at > to_at || (from_at == to_at && from_into > to_into)) {
    std::swap(from_at, to_at);
    std::swap(from_into, to_into);
  }
  const bool collapsed = from_at == to_at && from_into == to_into;
  if (collapsed || selection_touches_table(paragraphs, from_at, from_into, to_at, to_into))
    return place_collapsed(paragraphs, at, into, block);
  return replace_body(paragraphs, from_at, from_into, to_at, to_into, block);
}

int insert_pasted_paragraphs(std::vector<Paragraph>& paragraphs, size_t at, int into,
                             const std::vector<Paragraph>& block)
{
  return insert_pasted_paragraphs(paragraphs, at, into, at, into, at, into, block);
}

bool fit_table_widths(std::vector<int>& widths, int total, int limit)
{
  const int columns = static_cast<int>(widths.size());
  if (columns < 1)
    return false;
  const int floor_sum = kMinCellTwips * columns;
  if (limit < floor_sum)
    return false;
  const int target = std::max(floor_sum, std::min(total, limit));
  if (columns == 1) {
    widths[0] = target;
    return true;
  }
  long long sum = 0;
  for (int width : widths)
    sum += std::max(0, width);
  std::vector<int> next(static_cast<size_t>(columns), kMinCellTwips);
  if (sum <= 0) {
    const int each = target / columns;
    int used = 0;
    for (int i = 0; i + 1 < columns; ++i) {
      next[static_cast<size_t>(i)] = each;
      used += each;
    }
    next.back() = target - used;
  } else {
    long long used = 0;
    for (int i = 0; i + 1 < columns; ++i) {
      long long piece =
          (static_cast<long long>(std::max(0, widths[static_cast<size_t>(i)])) * target) / sum;
      if (piece < kMinCellTwips)
        piece = kMinCellTwips;
      if (piece > target)
        piece = target;
      next[static_cast<size_t>(i)] = static_cast<int>(piece);
      used += piece;
    }
    long long last = static_cast<long long>(target) - used;
    // The last column takes the remainder, and borrows from earlier columns
    // when that remainder is under the minimum.
    if (last < kMinCellTwips) {
      long long need = kMinCellTwips - last;
      last = kMinCellTwips;
      for (int i = columns - 2; i >= 0 && need > 0; --i) {
        int& earlier = next[static_cast<size_t>(i)];
        const long long spare = static_cast<long long>(earlier) - kMinCellTwips;
        const long long take = std::min(std::max(0LL, spare), need);
        earlier -= static_cast<int>(take);
        need -= take;
      }
    }
    next.back() = static_cast<int>(last);
  }
  widths = std::move(next);
  return true;
}

bool set_table_column_width(std::vector<int>& widths, int column, int width, int limit)
{
  const int columns = static_cast<int>(widths.size());
  if (columns < 1 || column < 0 || column >= columns)
    return false;
  const int floor_sum = kMinCellTwips * columns;
  if (limit < floor_sum)
    return false;
  const int max_column = limit - kMinCellTwips * (columns - 1);
  const int desired = std::max(kMinCellTwips, std::min(width, max_column));
  if (columns == 1) {
    widths[0] = desired;
    return true;
  }
  long long others = 0;
  for (int i = 0; i < columns; ++i) {
    if (i != column)
      others += std::max(0, widths[static_cast<size_t>(i)]);
  }
  if (static_cast<long long>(desired) + others <= limit) {
    widths[static_cast<size_t>(column)] = desired;
    return true;
  }
  std::vector<int> rest;
  rest.reserve(static_cast<size_t>(columns - 1));
  for (int i = 0; i < columns; ++i) {
    if (i != column)
      rest.push_back(std::max(0, widths[static_cast<size_t>(i)]));
  }
  const int room = limit - desired;
  if (!fit_table_widths(rest, room, room))
    return false;
  std::vector<int> next = widths;
  next[static_cast<size_t>(column)] = desired;
  size_t at = 0;
  for (int i = 0; i < columns; ++i) {
    if (i == column)
      continue;
    next[static_cast<size_t>(i)] = rest[at++];
  }
  widths = std::move(next);
  return true;
}

bool move_table_edge(std::vector<int>& widths, int column, int delta, int limit)
{
  const int columns = static_cast<int>(widths.size());
  if (column < 0 || column >= columns)
    return false;
  if (delta == 0)
    return true;
  if (column + 1 == columns) {
    long long sum = 0;
    for (int width : widths) {
      sum += std::max(0, width);
      if (sum > 2000000000LL)
        sum = 2000000000LL;
    }
    long long total = sum + static_cast<long long>(delta);
    if (total > 2000000000LL)
      total = 2000000000LL;
    if (total < 0)
      total = 0;
    return fit_table_widths(widths, static_cast<int>(total), limit);
  }
  const long long left = widths[static_cast<size_t>(column)];
  const long long right = widths[static_cast<size_t>(column + 1)];
  const long long pair = left + right;
  if (pair < static_cast<long long>(kMinCellTwips) * 2)
    return false;
  long long next = left + static_cast<long long>(delta);
  const long long low = kMinCellTwips;
  const long long high = pair - kMinCellTwips;
  if (next < low)
    next = low;
  if (next > high)
    next = high;
  widths[static_cast<size_t>(column)] = static_cast<int>(next);
  widths[static_cast<size_t>(column + 1)] = static_cast<int>(pair - next);
  return true;
}

bool apply_table_widths(std::vector<Paragraph>& paragraphs, size_t index,
                        const std::vector<int>& widths)
{
  if (!in_table(paragraphs, index))
    return false;
  const int id = paragraphs[index].cell.table;
  const int columns = paragraphs[index].cell.columns;
  if (columns < 1 || static_cast<int>(widths.size()) != columns)
    return false;
  bool any = false;
  for (Paragraph& paragraph : paragraphs) {
    if (paragraph.cell.table != id)
      continue;
    paragraph.cell.widths = widths;
    any = true;
  }
  return any;
}

}  // namespace writeit
