/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"

#include <algorithm>
#include <array>
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
  if (run.text.empty())
    return;
  if (!paragraph.runs.empty() && same_format(paragraph.runs.back(), run))
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

Paragraph parse_inlines(const std::string& text, const std::string& font, int size, int heading)
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

std::string inline_export(const Paragraph& paragraph)
{
  std::string out;
  for (const Run& run : paragraph.runs) {
    if (run.bold && run.italic)
      out += "***" + run.text + "***";
    else if (run.bold)
      out += "**" + run.text + "**";
    else if (run.italic)
      out += "*" + run.text + "*";
    else
      out += run.text;
  }
  return out;
}

}  // namespace

bool same_format(const Run& a, const Run& b)
{
  return a.font == b.font && a.size == b.size && a.bold == b.bold && a.italic == b.italic &&
         a.underline == b.underline;
}

bool operator==(const Run& a, const Run& b)
{
  return same_format(a, b) && a.text == b.text;
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
  return a.heading == b.heading && a.indents == b.indents && a.align == b.align &&
         a.list == b.list && a.runs == b.runs;
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
    if (x.heading != y.heading || x.indents != y.indents || x.align != y.align ||
        x.list.kind != y.list.kind || x.list.level != y.list.level || !(x.runs == y.runs))
      return false;
    numbered = numbered || x.list.kind == ListKind::Number;
  }
  // And the numbers they show, which is where the starts matter.
  return !numbered || (list_ids(a.paragraphs) == list_ids(b.paragraphs) &&
                       list_numbers(a.paragraphs) == list_numbers(b.paragraphs));
}

Document blank_document(const std::string& font, int size)
{
  (void)font;
  (void)size;
  Document doc;
  doc.paragraphs.push_back(Paragraph{});
  return doc;
}

Document plain_import(const std::string& text, const std::string& font, int size)
{
  Document doc;
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

Document markdown_import(const std::string& text, const std::string& font, int size)
{
  Document doc;
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
      doc.paragraphs.push_back(parse_inlines(body, font, size, level));
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
  for (const Paragraph& paragraph : doc.paragraphs) {
    const std::string body = inline_export(paragraph);
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
