/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"

#include "font_sizes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdint>
#include <map>
#include <iterator>

namespace writeit {
namespace {

std::string strip_cr(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c != '\r')
      out.push_back(c);
  }
  return out;
}

std::vector<std::string> lines_of(const std::string& text)
{
  const std::string clean = strip_cr(text);
  std::vector<std::string> lines;
  std::string line;
  for (char c : clean) {
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  if (!line.empty() || lines.empty())
    lines.push_back(line);
  return lines;
}

void add_run(Paragraph& paragraph, Run run)
{
  // A picture has no text, and neither it nor a footnote marker merges into
  // the run beside it.
  const bool held = !run.text.empty() || run.image.has_value() || run.note != 0;
  if (!held)
    return;
  const bool sticky = !run.image.has_value() && run.note == 0 && !paragraph.runs.empty() &&
                      !paragraph.runs.back().image.has_value() && paragraph.runs.back().note == 0 &&
                      same_format(paragraph.runs.back(), run);
  if (sticky)
    paragraph.runs.back().text += run.text;
  else
    paragraph.runs.push_back(std::move(run));
}

bool heading_marks(const std::string& line, int& level, std::string& body)
{
  size_t i = 0;
  while (i < line.size() && i < 6 && line[i] == '#')
    ++i;
  if (i == 0 || i == line.size() || (line[i] != ' ' && line[i] != '\t'))
    return false;
  level = static_cast<int>(i);
  ++i;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    ++i;
  body = line.substr(i);
  return true;
}

Paragraph parse_inlines(const std::string& text, const std::string& font, double size, int heading)
{
  Paragraph paragraph;
  paragraph.heading = heading;
  bool bold = false;
  bool italic = false;
  std::string buf;
  auto flush = [&]() {
    Run run;
    run.text = buf;
    run.font = font;
    run.size = size;
    run.bold = bold;
    run.italic = italic;
    add_run(paragraph, run);
    buf.clear();
  };
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '!' && i + 1 < text.size() && text[i + 1] == '[') {
      const size_t alt_end = text.find("](", i + 2);
      const size_t path_end =
          alt_end == std::string::npos ? std::string::npos : text.find(')', alt_end + 2);
      if (alt_end != std::string::npos && path_end != std::string::npos) {
        flush();
        Run run;
        run.font = font;
        run.size = size;
        run.bold = bold;
        run.italic = italic;
        Image image;
        image.alt = text.substr(i + 2, alt_end - (i + 2));
        image.path = text.substr(alt_end + 2, path_end - (alt_end + 2));
        load_image_file(image.path, image);
        run.image = std::move(image);
        paragraph.runs.push_back(std::move(run));
        i = path_end + 1;
        continue;
      }
    }
    if (text.compare(i, 3, "***") == 0 || text.compare(i, 3, "___") == 0) {
      flush();
      bold = !bold;
      italic = !italic;
      i += 3;
      continue;
    }
    if (text.compare(i, 2, "**") == 0 || text.compare(i, 2, "__") == 0) {
      flush();
      bold = !bold;
      i += 2;
      continue;
    }
    if (text[i] == '*' || text[i] == '_') {
      flush();
      italic = !italic;
      ++i;
      continue;
    }
    buf.push_back(text[i]);
    ++i;
  }
  flush();
  return paragraph;
}

// Emphasis is written where a run differs from its paragraph's style, so a
// heading whose style is bold is not wrapped in **.
std::string inline_export(const Paragraph& paragraph, const Run& style)
{
  // Runs that look alike are one run here, so emphasis is not broken up.
  std::vector<Run> runs;
  for (const Run& run : paragraph.runs) {
    if (run.image.has_value()) {
      runs.push_back(run);
      continue;
    }
    if (!runs.empty() && !runs.back().image.has_value() && runs.back().bold == run.bold &&
        runs.back().italic == run.italic)
      runs.back().text += run.text;
    else
      runs.push_back(run);
  }
  std::string out;
  for (const Run& run : runs) {
    if (run.image.has_value()) {
      if (!run.image->path.empty())
        out += "![" + run.image->alt + "](" + run.image->path + ")";
      continue;
    }
    const bool bold = run.bold && !style.bold;
    const bool italic = run.italic && !style.italic;
    if (bold && italic)
      out += "***" + run.text + "***";
    else if (bold)
      out += "**" + run.text + "**";
    else if (italic)
      out += "*" + run.text + "*";
    else
      out += run.text;
  }
  return out;
}

bool same_name(const std::string& a, const std::string& b)
{
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

// One well-formed UTF-8 sequence at i: advances i and returns true, or
// leaves i alone and returns false.
bool decode_one(const std::string& text, size_t& i, uint32_t& cp)
{
  const auto c = static_cast<unsigned char>(text[i]);
  size_t len = 0;
  uint32_t min = 0;
  if (c < 0x80) {
    cp = c;
    ++i;
    return true;
  }
  if ((c & 0xE0) == 0xC0) {
    len = 2;
    cp = c & 0x1F;
    min = 0x80;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    cp = c & 0x0F;
    min = 0x800;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    cp = c & 0x07;
    min = 0x10000;
  } else {
    return false;
  }
  if (i + len > text.size())
    return false;
  for (size_t k = 1; k < len; ++k) {
    const auto b = static_cast<unsigned char>(text[i + k]);
    if ((b & 0xC0) != 0x80)
      return false;
    cp = (cp << 6) | (b & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
    return false;
  i += len;
  return true;
}

// At most `bytes` bytes of valid UTF-8 `text`, never splitting a character.
std::string cut_utf8(const std::string& text, size_t bytes)
{
  if (text.size() <= bytes)
    return text;
  size_t end = bytes;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
    --end;
  return text.substr(0, end);
}

// The style a paragraph is in: its own, or Normal for a name the sheet does
// not know.
const Style& paragraph_style(const std::vector<Style>& sheet, const Paragraph& paragraph)
{
  if (const Style* style = find_style(sheet, paragraph.style))
    return *style;
  if (const Style* normal = find_style(sheet, kNormalStyle))
    return *normal;
  return sheet.front();
}

// Whether an attribute is the paragraph's or run's own: set directly, or
// different from the style it was in (as a file read it). An attribute
// found to be its own is marked so, so it stays even when a later style
// passes through its value.
bool own(unsigned& direct, unsigned bit, bool differs)
{
  if ((direct & bit) || differs) {
    direct |= bit;
    return true;
  }
  return false;
}

void restyle_indents(Indents& indents, unsigned& direct, const Indents& from, const Indents& to)
{
  if (!own(direct, kDirectLeft, indents.left != from.left))
    indents.left = to.left;
  if (!own(direct, kDirectRight, indents.right != from.right))
    indents.right = to.right;
  if (!own(direct, kDirectFirst, indents.first != from.first))
    indents.first = to.first;
}

// Attribute by attribute, as restyle_run() does for a run.
void restyle_paragraph(Paragraph& paragraph, const Style& from, const Style& to)
{
  for (Run& run : paragraph.runs)
    restyle_run(run, from, to);
  // Runs the style made alike are one run, as a reader would read them.
  std::vector<Run> merged;
  for (Run& run : paragraph.runs) {
    if (!merged.empty() && same_format(merged.back(), run))
      merged.back().text += run.text;
    else
      merged.push_back(std::move(run));
  }
  paragraph.runs = std::move(merged);
  // An empty paragraph's own format (its mark) moves as its text would.
  if (paragraph.mark)
    restyle_run(*paragraph.mark, from, to);
  if (paragraph.list.kind == ListKind::None) {
    restyle_indents(paragraph.indents, paragraph.direct, from.indents, to.indents);
    paragraph.indents = clamp_indents(paragraph.indents);
  } else {
    // The list sets the left and first-line indents; the style still gives
    // the right one, as in Word 97.
    if (!own(paragraph.direct, kDirectRight, paragraph.indents.right != from.indents.right))
      paragraph.indents.right = to.indents.right;
    paragraph.indents = clamp_indents(paragraph.indents);
    if (paragraph.list.has_own) {
      unsigned own_direct = paragraph.direct;
      restyle_indents(paragraph.list.own, own_direct, from.indents, to.indents);
      paragraph.list.own = clamp_indents(paragraph.list.own);
    }
  }
  if (!own(paragraph.direct, kDirectAlign, paragraph.align != from.align))
    paragraph.align = to.align;
  if (paragraph.heading == from.heading)
    paragraph.heading = to.heading;
  paragraph.style = to.name;
}

// A style based on one that changed takes the change wherever it agreed with
// its base. Its name, base, next style and outline level are its own.
Style restyle_style(Style style, const Style& from, const Style& to)
{
  // A style's attributes are its own where they differ from its base.
  Run format = style.format;
  format.direct = 0;
  restyle_run(format, from, to);
  format.direct = 0;
  style.format = format;
  unsigned none = 0;
  restyle_indents(style.indents, none, from.indents, to.indents);
  if (style.align == from.align)
    style.align = to.align;
  return style;
}

// Puts `updated` in place of sheet[index], carries the change to the
// paragraphs in that style, renames references to it, and goes on to the
// styles based on it. `visited` stops a circle a hand-made sheet might hold.
void carry_style(Document& doc, std::vector<Style>& sheet, size_t index, const Style& updated,
                 std::vector<bool>& visited)
{
  if (visited[index])
    return;
  visited[index] = true;
  const Style old = sheet[index];
  sheet[index] = updated;
  for (Paragraph& paragraph : doc.paragraphs) {
    if (same_name(paragraph.style, old.name))
      restyle_paragraph(paragraph, old, updated);
  }
  if (old.name != updated.name) {
    for (Style& style : sheet) {
      if (same_name(style.based_on, old.name))
        style.based_on = updated.name;
      if (same_name(style.next, old.name))
        style.next = updated.name;
    }
  }
  for (size_t i = 0; i < sheet.size(); ++i) {
    if (i == index || visited[i] || !same_name(sheet[i].based_on, updated.name))
      continue;
    carry_style(doc, sheet, i, restyle_style(sheet[i], old, updated), visited);
  }
}

}  // namespace

bool same_image(const std::optional<Image>& a, const std::optional<Image>& b)
{
  if (a.has_value() != b.has_value())
    return false;
  if (!a)
    return true;
  return a->path == b->path && a->alt == b->alt && a->type == b->type && a->data == b->data &&
         a->width == b->width && a->height == b->height;
}

bool same_look(const Run& a, const Run& b)
{
  return a.font == b.font && a.size == b.size && a.bold == b.bold && a.italic == b.italic &&
         a.underline == b.underline && a.note == b.note && same_image(a.image, b.image);
}

bool same_format(const Run& a, const Run& b)
{
  return same_look(a, b) && a.direct == b.direct;
}

bool same_mark(const Paragraph& a, const Paragraph& b)
{
  // Only an empty paragraph has a format of its own; text has its runs'.
  if (!a.runs.empty() || !b.runs.empty())
    return true;
  if (a.mark.has_value() != b.mark.has_value())
    return false;
  // By value, as runs compare: what was set directly is not saved.
  return !a.mark || same_look(*a.mark, *b.mark);
}

// Runs compare as a reader would read them: by value, adjacent runs that
// look alike counting as one.
bool operator==(const Run& a, const Run& b)
{
  return same_look(a, b) && a.text == b.text;
}

static bool same_runs(const std::vector<Run>& a, const std::vector<Run>& b)
{
  auto looks = [](const std::vector<Run>& runs) {
    std::vector<Run> out;
    for (const Run& run : runs) {
      if (run.text.empty() && !run.image.has_value() && run.note == 0)
        continue;
      const bool sticky = !run.image.has_value() && run.note == 0 && !out.empty() &&
                          !out.back().image.has_value() && out.back().note == 0 &&
                          same_look(out.back(), run);
      if (sticky)
        out.back().text += run.text;
      else
        out.push_back(run);
    }
    return out;
  };
  return looks(a) == looks(b);
}

bool operator==(const Indents& a, const Indents& b)
{
  return a.left == b.left && a.right == b.right && a.first == b.first;
}

bool operator!=(const Indents& a, const Indents& b)
{
  return !(a == b);
}

bool operator==(const ListFormat& a, const ListFormat& b)
{
  return a.kind == b.kind && a.level == b.level &&
         (a.kind != ListKind::Number || (a.list == b.list && a.start == b.start));
}

bool operator!=(const ListFormat& a, const ListFormat& b)
{
  return !(a == b);
}

bool operator==(const Paragraph& a, const Paragraph& b)
{
  return a.heading == b.heading && a.style == b.style && a.indents == b.indents &&
         a.align == b.align && a.list == b.list && a.cell == b.cell &&
         a.page_break == b.page_break && same_runs(a.runs, b.runs) && same_mark(a, b);
}

bool indents_fit(const Indents& indents)
{
  return indents.left + indents.first >= 0;
}

Indents clamp_indents(Indents indents)
{
  indents.left = std::max(0, std::min(kMaxIndent, indents.left));
  indents.right = std::max(0, std::min(kMaxIndent, indents.right));
  indents.first = std::max(-indents.left, std::min(kMaxIndent, indents.first));
  return indents;
}

ListFormat clamp_list(ListFormat list)
{
  if (list.kind != ListKind::Bullet && list.kind != ListKind::Number)
    return ListFormat{};
  list.level = std::max(0, std::min(kListLevels - 1, list.level));
  if (list.kind != ListKind::Number || list.list < 0)
    list.list = 0;
  list.start = list.kind == ListKind::Number ? std::max(0, std::min(kMaxListStart, list.start)) : 1;
  list.own = list.has_own ? clamp_indents(list.own) : Indents{};
  return list;
}

Indents list_indents(int level)
{
  level = std::max(0, std::min(kListLevels - 1, level));
  Indents indents;
  indents.left = kListStep * (level + 1);
  indents.first = -kListHang;
  return indents;
}

namespace {

// The start of each list level, by (list id, level): its first item's.
std::map<std::pair<int, int>, int> level_starts(const std::vector<Paragraph>& paragraphs,
                                                const std::vector<int>& ids)
{
  std::map<std::pair<int, int>, int> starts;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const ListFormat list = clamp_list(paragraphs[i].list);
    if (list.kind == ListKind::Number)
      starts.emplace(std::make_pair(ids[i], list.level), list.start);
  }
  return starts;
}

}  // namespace

namespace {

// list_numbers()' counters before a level has counted.
const std::array<int, kListLevels> kNotCounted = [] {
  std::array<int, kListLevels> all{};
  all.fill(-1);
  return all;
}();

}  // namespace

std::vector<int> list_numbers(const std::vector<Paragraph>& paragraphs)
{
  std::vector<int> numbers;
  numbers.reserve(paragraphs.size());
  // One set of level counters per list. Plain paragraphs and bullets leave
  // them alone, so a list counts on past them.
  // Each level starts at its first item's start, which may be 0; -1 marks a
  // level that has not counted yet under its current parent.
  const std::vector<int> ids = list_ids(paragraphs);
  const auto starts = level_starts(paragraphs, ids);
  std::vector<std::array<int, kListLevels>> counters;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const ListFormat list = clamp_list(paragraphs[i].list);
    if (list.kind != ListKind::Number) {
      numbers.push_back(0);
      continue;
    }
    const size_t id = static_cast<size_t>(ids[i]);
    if (counters.size() <= id)
      counters.resize(id + 1, kNotCounted);
    std::array<int, kListLevels>& count = counters[id];
    int& here = count[static_cast<size_t>(list.level)];
    if (here < 0)
      here = starts.at(std::make_pair(ids[i], list.level));
    // Paragraph counts cannot reach INT_MAX, but stay defined if they did.
    else if (here < 2000000000)
      ++here;
    for (int deeper = list.level + 1; deeper < kListLevels; ++deeper)
      count[static_cast<size_t>(deeper)] = -1;
    numbers.push_back(count[static_cast<size_t>(list.level)]);
  }
  return numbers;
}

std::vector<int> list_ids(const std::vector<Paragraph>& paragraphs)
{
  std::vector<int> ids;
  ids.reserve(paragraphs.size());
  // Label to id, in order of first appearance.
  std::map<int, int> seen;
  int previous = 0;
  for (const Paragraph& paragraph : paragraphs) {
    const ListFormat list = clamp_list(paragraph.list);
    if (list.kind != ListKind::Number) {
      ids.push_back(0);
      continue;
    }
    int id = previous;
    if (list.list != 0 || previous == 0) {
      // The document's first numbered item with no label begins list 1.
      const int label = list.list != 0 ? list.list : 1;
      const auto found = seen.find(label);
      if (found != seen.end()) {
        id = found->second;
      } else if (seen.size() < static_cast<size_t>(kMaxLists)) {
        id = static_cast<int>(seen.size()) + 1;
        seen.emplace(label, id);
      } else {
        id = kMaxLists;
      }
    }
    ids.push_back(id);
    previous = id;
  }
  return ids;
}

void canonical_lists(std::vector<Paragraph>& paragraphs)
{
  const std::vector<int> ids = list_ids(paragraphs);
  for (size_t i = 0; i < paragraphs.size(); ++i)
    paragraphs[i].list.list = ids[i];
}

namespace {

// Moves the item at `index` and the rest of its list into list `to`.
void move_rest_of_list(std::vector<Paragraph>& paragraphs, const std::vector<int>& ids,
                       size_t index, int to)
{
  const int from = ids[index];
  canonical_lists(paragraphs);
  for (size_t i = index; i < paragraphs.size(); ++i)
    if (ids[i] == from)
      paragraphs[i].list.list = to;
  canonical_lists(paragraphs);
}

}  // namespace

bool restart_numbering(std::vector<Paragraph>& paragraphs, size_t index)
{
  if (index >= paragraphs.size() || clamp_list(paragraphs[index].list).kind != ListKind::Number)
    return false;
  const std::vector<int> ids = list_ids(paragraphs);
  const int newest = *std::max_element(ids.begin(), ids.end());
  const bool starts_here =
      std::find(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(index), ids[index]) ==
      ids.begin() + static_cast<std::ptrdiff_t>(index);
  if (starts_here || newest >= kMaxLists)
    return false;
  // The new list starts where the old one did, level by level.
  const auto starts = level_starts(paragraphs, ids);
  for (size_t i = index; i < paragraphs.size(); ++i) {
    if (ids[i] != ids[index])
      continue;
    const auto found = starts.find(std::make_pair(ids[i], clamp_list(paragraphs[i].list).level));
    if (found != starts.end())
      paragraphs[i].list.start = found->second;
  }
  move_rest_of_list(paragraphs, ids, index, newest + 1);
  return true;
}

bool continue_numbering(std::vector<Paragraph>& paragraphs, size_t index)
{
  if (index >= paragraphs.size() || clamp_list(paragraphs[index].list).kind != ListKind::Number)
    return false;
  const std::vector<int> ids = list_ids(paragraphs);
  for (size_t above = index; above-- > 0;) {
    if (ids[above] != 0 && ids[above] != ids[index]) {
      move_rest_of_list(paragraphs, ids, index, ids[above]);
      return true;
    }
  }
  return false;
}

std::string list_label(const ListFormat& raw, int number)
{
  const ListFormat list = clamp_list(raw);
  if (list.kind == ListKind::None)
    return {};
  if (list.kind == ListKind::Bullet) {
    // U+2022 bullet, U+25E6 white bullet, U+25AA small black square, in UTF-8.
    static const char* const kBullets[] = {"\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA"};
    return kBullets[list.level % 3];
  }
  // Letters and roman numerals have no zero: a level that starts at 0
  // (Word's \levelstartat0) shows 0 in any style.
  number = std::max(0, number);
  const int style = list.level % 3;
  std::string text;
  if (number == 0) {
    text = "0";
  } else if (style == 1) {
    // a ... z, aa, ab ...: bijective base 26, as Word letters.
    unsigned value = static_cast<unsigned>(number);
    while (value > 0) {
      --value;
      text.insert(text.begin(), static_cast<char>('a' + value % 26));
      value /= 26;
    }
  } else if (style == 2 && number < 4000) {
    static const int kValues[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* const kNumerals[] = {"m",  "cm", "d",  "cd", "c",  "xc", "l",
                                            "xl", "x",  "ix", "v",  "iv", "i"};
    int value = number;
    for (size_t i = 0; i < sizeof(kValues) / sizeof(kValues[0]); ++i) {
      while (value >= kValues[i]) {
        text += kNumerals[i];
        value -= kValues[i];
      }
    }
  } else {
    text = std::to_string(number);
  }
  return text + ".";
}

namespace {

bool at_list_indents(const Indents& indents, int level)
{
  const Indents standard = list_indents(level);
  return indents.left == standard.left && indents.first == standard.first;
}

void join_list(Paragraph& paragraph, ListKind kind)
{
  if (paragraph.list.kind != ListKind::None) {
    // Switching between bullets and numbers keeps the level and indents. A
    // new number joins the numbered list above, as one added in Word does.
    paragraph.list = clamp_list(paragraph.list);
    paragraph.list.kind = kind;
    paragraph.list.list = 0;
    paragraph.list.start = 1;
    return;
  }
  paragraph.list.kind = kind;
  paragraph.list.level = 0;
  paragraph.list.list = 0;
  paragraph.list.start = 1;
  paragraph.list.has_own = true;
  paragraph.list.own = clamp_indents(paragraph.indents);
  Indents indents = paragraph.indents;
  if (indents.left == 0 && indents.first == 0) {
    indents.left = list_indents(0).left;
  } else {
    // Already indented: the text stays at the left indent and the label
    // hangs in front of it.
    indents.left = std::max(kListHang, indents.left);
  }
  indents.first = -kListHang;
  paragraph.indents = clamp_indents(indents);
}

void leave_list(Paragraph& paragraph)
{
  if (paragraph.list.kind == ListKind::None)
    return;
  const ListFormat list = clamp_list(paragraph.list);
  paragraph.list = ListFormat{};
  if (list.has_own) {
    paragraph.indents = list.own;
    return;
  }
  // No memory of the paragraph's own indents, as for an item read from a
  // file: take off the list's indents and the label's hang.
  if (at_list_indents(paragraph.indents, list.level))
    paragraph.indents.left = 0;
  paragraph.indents.first = 0;
  paragraph.indents = clamp_indents(paragraph.indents);
}

}  // namespace

ListKind toggle_list(std::vector<Paragraph>& paragraphs, ListKind kind)
{
  if (paragraphs.empty())
    return ListKind::None;
  if (kind != ListKind::Bullet && kind != ListKind::Number)
    return clamp_list(paragraphs.front().list).kind;
  const bool all = std::all_of(paragraphs.begin(), paragraphs.end(), [kind](const Paragraph& p) {
    return clamp_list(p.list).kind == kind;
  });
  for (Paragraph& paragraph : paragraphs) {
    if (all)
      leave_list(paragraph);
    else
      join_list(paragraph, kind);
  }
  return all ? ListKind::None : kind;
}

void set_list_level(Paragraph& paragraph, int level)
{
  const ListFormat old = clamp_list(paragraph.list);
  if (old.kind == ListKind::None)
    return;
  level = std::max(0, std::min(kListLevels - 1, level));
  if (at_list_indents(paragraph.indents, old.level)) {
    const int right = paragraph.indents.right;
    paragraph.indents = list_indents(level);
    paragraph.indents.right = right;
  } else {
    paragraph.indents.left += kListStep * (level - old.level);
    paragraph.indents = clamp_indents(paragraph.indents);
  }
  paragraph.list = old;
  paragraph.list.level = level;
}

bool operator==(const Document& a, const Document& b)
{
  // List ids are labels: two documents are equal when their numbered items
  // fall into the same lists, whatever the labels say.
  if (a.paragraphs.size() != b.paragraphs.size())
    return false;
  bool numbered = false;
  for (size_t i = 0; i < a.paragraphs.size(); ++i) {
    const Paragraph& x = a.paragraphs[i];
    const Paragraph& y = b.paragraphs[i];
    if (x.heading != y.heading || x.style != y.style || x.indents != y.indents ||
        x.align != y.align || x.list.kind != y.list.kind || x.list.level != y.list.level ||
        x.cell != y.cell || x.page_break != y.page_break || !same_runs(x.runs, y.runs) ||
        !same_mark(x, y))
      return false;
    numbered = numbered || x.list.kind == ListKind::Number;
  }
  // And the numbers they show, which is where the starts matter.
  if (numbered && (list_ids(a.paragraphs) != list_ids(b.paragraphs) ||
                   list_numbers(a.paragraphs) != list_numbers(b.paragraphs)))
    return false;
  if (!(a.page == b.page) || a.header != b.header || a.footer != b.footer || a.notes != b.notes)
    return false;
  return style_sheet(a) == style_sheet(b);
}

int list_label_x(Align align, int hang_x, int text_x, int label_width, int space, int gap)
{
  // Only centred and right-aligned text moves; anything else, Justify when
  // it lands included, starts at the indent like Left.
  if (align != Align::Center && align != Align::Right)
    return hang_x;
  const int width = std::max(0, label_width);
  const int before = std::max(std::max(0, space), width + std::max(0, gap));
  return std::max(0, text_x - before);
}

bool operator==(const Style& a, const Style& b)
{
  return a.name == b.name && a.based_on == b.based_on && a.next == b.next &&
         same_format(a.format, b.format) && a.indents == b.indents && a.align == b.align &&
         a.heading == b.heading;
}

bool operator!=(const Style& a, const Style& b)
{
  return !(a == b);
}

std::vector<Style> builtin_styles(const std::string& font, double size)
{
  const std::string face = font.empty() ? "Sans" : font;
  // In half points, as RTF's \fsN, so a 10.5 pt Normal stays 10.5 pt.
  const int base = std::min(2 * kMaxStyleSize, half_points_of(size));
  auto style = [&](const char* name, int half_points, bool bold, bool italic) {
    Style s;
    s.name = name;
    s.format.font = face;
    s.format.size = size_from_half_points(std::min(2 * kMaxStyleSize, half_points));
    s.format.bold = bold;
    s.format.italic = italic;
    if (std::string(name) != kNormalStyle)
      s.based_on = kNormalStyle;
    return s;
  };
  std::vector<Style> sheet;
  sheet.push_back(style(kNormalStyle, base, false, false));
  // Five points up for the first level, then three, two, and one for the
  // rest, so no heading shrinks to body text (M1 showed every level larger,
  // and Word 97's Heading 4 is 12 pt bold over 10 pt Normal): bold, then bold
  // italic and italic for the last two, as AbiWord's and LibreOffice's sets
  // step down.
  const int sizes[] = {base + 10, base + 6, base + 4, base + 2, base + 2, base + 2};
  const bool bolds[] = {true, true, true, true, true, false};
  const bool italics[] = {false, false, false, false, true, true};
  for (int level = 1; level <= 6; ++level) {
    const std::string name = "Heading " + std::to_string(level);
    Style h = style(name.c_str(), sizes[level - 1], bolds[level - 1], italics[level - 1]);
    h.heading = level;
    h.next = kNormalStyle;
    sheet.push_back(h);
  }
  Style block = style("Block Text", base, false, false);
  block.indents = Indents{1440, 1440, 0};
  sheet.push_back(block);
  Style plain = style("Plain Text", base - 2, false, false);
  plain.format.font = "Monospace";
  sheet.push_back(plain);
  return sheet;
}

const std::vector<Style>& default_styles()
{
  static const std::vector<Style> kDefault = builtin_styles("Sans", 11);
  return kDefault;
}

const std::vector<Style>& style_sheet(const Document& doc)
{
  return doc.styles.empty() ? default_styles() : doc.styles;
}

std::vector<Style> complete_sheet(std::vector<Style> sheet)
{
  auto normal = std::find_if(sheet.begin(), sheet.end(),
                             [](const Style& s) { return same_name(s.name, kNormalStyle); });
  if (normal == sheet.end()) {
    sheet.insert(sheet.begin(), default_styles().front());
  } else {
    Style first = *normal;
    sheet.erase(normal);
    first.name = kNormalStyle;
    first.based_on.clear();
    sheet.insert(sheet.begin(), first);
  }
  const Style& front = sheet.front();
  for (const Style& builtin : builtin_styles(front.format.font, front.format.size)) {
    if (!find_style(sheet, builtin.name))
      sheet.push_back(builtin);
  }
  return sheet;
}

const Style* find_style(const std::vector<Style>& sheet, const std::string& name)
{
  for (const Style& style : sheet) {
    if (same_name(style.name, name))
      return &style;
  }
  return nullptr;
}

std::string clean_style_name(const std::string& name)
{
  std::string out;
  size_t i = 0;
  while (i < name.size()) {
    const size_t begin = i;
    uint32_t cp = 0;
    if (!decode_one(name, i, cp)) {
      ++i;
      continue;
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F))
      continue;
    out.append(name, begin, i - begin);
  }
  size_t first = 0;
  while (first < out.size() && out[first] == ' ')
    ++first;
  size_t last = out.size();
  while (last > first && out[last - 1] == ' ')
    --last;
  out = out.substr(first, last - first);
  out = cut_utf8(out, kMaxStyleName);
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

std::string unique_style_name(const std::vector<Style>& sheet, const std::string& wanted)
{
  if (!find_style(sheet, wanted))
    return wanted;
  for (size_t n = 2;; ++n) {
    const std::string suffix = " (" + std::to_string(n) + ")";
    const std::string base =
        cut_utf8(wanted, kMaxStyleName > suffix.size() ? kMaxStyleName - suffix.size() : 0);
    const std::string candidate = base + suffix;
    if (!find_style(sheet, candidate))
      return candidate;
  }
}

std::string next_style(const std::vector<Style>& sheet, const std::string& name)
{
  const Style* style = find_style(sheet, name);
  if (!style)
    return name;
  const Style* next = style->next.empty() ? nullptr : find_style(sheet, style->next);
  return next ? next->name : style->name;
}

void restyle_run(Run& run, const Style& from, const Style& to)
{
  const Run& f = from.format;
  if (!own(run.direct, kDirectFont, run.font != f.font))
    run.font = to.format.font;
  if (!own(run.direct, kDirectSize, run.size != f.size))
    run.size = to.format.size;
  if (!own(run.direct, kDirectBold, run.bold != f.bold))
    run.bold = to.format.bold;
  if (!own(run.direct, kDirectItalic, run.italic != f.italic))
    run.italic = to.format.italic;
  if (!own(run.direct, kDirectUnderline, run.underline != f.underline))
    run.underline = to.format.underline;
}

bool apply_style(Document& doc, size_t first, size_t last, const std::string& name)
{
  const std::vector<Style> sheet = style_sheet(doc);
  const Style* to = find_style(sheet, name);
  if (!to || first > last || first >= doc.paragraphs.size())
    return false;
  last = std::min(last, doc.paragraphs.size() - 1);
  for (size_t i = first; i <= last; ++i) {
    Paragraph& paragraph = doc.paragraphs[i];
    restyle_paragraph(paragraph, paragraph_style(sheet, paragraph), *to);
  }
  return true;
}

bool update_style(Document& doc, const std::string& name, const Style& changed)
{
  std::vector<Style> sheet = style_sheet(doc);
  const Style* found = find_style(sheet, name);
  if (!found)
    return false;
  const size_t index = static_cast<size_t>(found - sheet.data());
  const Style old = sheet[index];
  Style updated = changed;
  updated.name = clean_style_name(changed.name);
  updated.format.size =
      size_from_half_points(std::min(2 * kMaxStyleSize, half_points_of(updated.format.size)));
  if (updated.name.empty())
    return false;
  if (same_name(old.name, kNormalStyle) && updated.name != kNormalStyle)
    return false;
  for (size_t i = 0; i < sheet.size(); ++i) {
    if (i != index && same_name(sheet[i].name, updated.name))
      return false;
  }
  if (!updated.based_on.empty()) {
    const Style* base = find_style(sheet, updated.based_on);
    if (!base || base == &sheet[index] || same_name(updated.based_on, updated.name))
      return false;
    updated.based_on = base->name;
    // Walking up from the new base must not come back to this style.
    const Style* at = base;
    for (size_t steps = 0; at && steps <= sheet.size(); ++steps) {
      if (at == &sheet[index])
        return false;
      at = at->based_on.empty() ? nullptr : find_style(sheet, at->based_on);
    }
  }
  if (!updated.next.empty()) {
    if (same_name(updated.next, old.name) || same_name(updated.next, updated.name)) {
      updated.next.clear();  // itself, as the reader and the built-ins have it
    } else {
      const Style* next = find_style(sheet, updated.next);
      if (!next)
        return false;
      updated.next = next->name;
    }
  }
  std::vector<bool> visited(sheet.size(), false);
  carry_style(doc, sheet, index, updated, visited);
  doc.styles = sheet;
  return true;
}

bool add_style(Document& doc, const Style& style)
{
  std::vector<Style> sheet = style_sheet(doc);
  Style added = style;
  added.name = clean_style_name(style.name);
  added.format.size =
      size_from_half_points(std::min(2 * kMaxStyleSize, half_points_of(added.format.size)));
  if (added.name.empty() || find_style(sheet, added.name) || sheet.size() >= kMaxStyles)
    return false;
  if (!added.based_on.empty()) {
    const Style* base = find_style(sheet, added.based_on);
    if (!base)
      return false;
    added.based_on = base->name;
  }
  if (same_name(added.next, added.name)) {
    added.next.clear();  // itself
  } else if (!added.next.empty()) {
    const Style* next = find_style(sheet, added.next);
    if (!next)
      return false;
    added.next = next->name;
  }
  sheet.push_back(added);
  doc.styles = sheet;
  return true;
}

void adopt_sheet(Document& doc, const std::string& font, double size)
{
  if (!doc.styles.empty())
    return;
  // The font and size carrying the most characters, ties to the first. Sizes
  // in half points, so 10.5 pt text gives a 10.5 pt sheet.
  std::vector<std::pair<std::pair<std::string, int>, size_t>> counts;
  for (const Paragraph& paragraph : doc.paragraphs) {
    for (const Run& run : paragraph.runs) {
      if (run.text.empty())
        continue;
      const auto key = std::make_pair(run.font, half_points_of(run.size));
      auto found = std::find_if(counts.begin(), counts.end(),
                                [&key](const auto& item) { return item.first == key; });
      if (found == counts.end())
        counts.emplace_back(key, run.text.size());
      else
        found->second += run.text.size();
    }
  }
  std::string chosen_font = font;
  int chosen_size = half_points_of(size);
  size_t best = 0;
  for (const auto& item : counts) {
    if (item.second > best) {
      best = item.second;
      chosen_font = item.first.first;
      chosen_size = item.first.second;
    }
  }
  doc.styles = builtin_styles(chosen_font, size_from_half_points(chosen_size));
}

void adopt_heading_styles(Document& doc, const std::string& font, double size)
{
  if (!doc.styles.empty())
    return;
  const bool any = std::any_of(doc.paragraphs.begin(), doc.paragraphs.end(),
                               [](const Paragraph& p) { return p.heading >= 1 && p.heading <= 6; });
  if (!any)
    return;
  adopt_sheet(doc, font, size);
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    const Paragraph& paragraph = doc.paragraphs[i];
    if (paragraph.heading < 1 || paragraph.heading > 6 || paragraph.style != kNormalStyle)
      continue;
    apply_style(doc, i, i, "Heading " + std::to_string(paragraph.heading));
  }
}

int list_centre_from(Align align, int hang_x, int label_width, int space, int gap)
{
  if (align != Align::Center)
    return -1;
  const long long before = std::max<long long>(
      std::max(0, space), static_cast<long long>(std::max(0, label_width)) + std::max(0, gap));
  return static_cast<int>(std::min<long long>(std::max(0, hang_x) + before, INT_MAX));
}

int list_text_start(const Indents& raw)
{
  const Indents indents = clamp_indents(raw);
  return std::max(indents.left, indents.left + indents.first + kListHang);
}

int list_label_space(const Indents& raw)
{
  const Indents indents = clamp_indents(raw);
  return list_text_start(indents) - (indents.left + indents.first);
}

Document blank_document(const std::string& font, double size)
{
  Document doc;
  doc.styles = builtin_styles(font, size);
  doc.paragraphs.push_back(Paragraph{});
  return doc;
}

Document plain_import(const std::string& text, const std::string& font, double size)
{
  Document doc;
  doc.styles = builtin_styles(font, size);
  const std::string clean = strip_cr(text);
  std::string line;
  auto push = [&]() {
    Paragraph paragraph;
    if (!line.empty()) {
      Run run;
      run.text = line;
      run.font = font;
      run.size = size;
      paragraph.runs.push_back(run);
    }
    doc.paragraphs.push_back(paragraph);
    line.clear();
  };
  if (clean.empty()) {
    doc.paragraphs.push_back(Paragraph{});
    return doc;
  }
  for (char c : clean) {
    if (c == '\n')
      push();
    else
      line.push_back(c);
  }
  if (!clean.empty() && clean.back() != '\n')
    push();
  if (doc.paragraphs.empty())
    doc.paragraphs.push_back(Paragraph{});
  return doc;
}

Document markdown_import(const std::string& text, const std::string& font, double size)
{
  Document doc;
  doc.styles = builtin_styles(font, size);
  const std::vector<std::string> lines = lines_of(text);
  size_t i = 0;
  while (i < lines.size()) {
    if (lines[i].empty()) {
      ++i;
      continue;
    }
    int level = 0;
    std::string body;
    if (heading_marks(lines[i], level, body)) {
      // A heading takes its Heading style, as if applied to body text.
      Paragraph paragraph = parse_inlines(body, font, size, level);
      const std::string name = "Heading " + std::to_string(level);
      restyle_paragraph(paragraph, doc.styles.front(), *find_style(doc.styles, name));
      doc.paragraphs.push_back(paragraph);
      ++i;
      continue;
    }
    std::string joined;
    while (i < lines.size() && !lines[i].empty() && !heading_marks(lines[i], level, body)) {
      if (!joined.empty())
        joined.push_back(' ');
      joined += lines[i];
      ++i;
    }
    doc.paragraphs.push_back(parse_inlines(joined, font, size, 0));
  }
  if (doc.paragraphs.empty())
    doc.paragraphs.push_back(Paragraph{});
  return doc;
}

std::string markdown_export(const Document& doc)
{
  std::string out;
  bool any = false;
  const std::vector<Style> sheet = style_sheet(doc);
  for (const Paragraph& paragraph : doc.paragraphs) {
    const std::string body = inline_export(paragraph, paragraph_style(sheet, paragraph).format);
    if (paragraph.heading == 0 && body.empty())
      continue;
    if (any)
      out += "\n\n";
    any = true;
    if (paragraph.heading >= 1 && paragraph.heading <= 6) {
      out.append(static_cast<size_t>(paragraph.heading), '#');
      out += " ";
    }
    out += body;
  }
  return out;
}

}  // namespace writeit
