/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"

#include <algorithm>
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
  return a.kind == b.kind && a.level == b.level;
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

std::vector<int> list_numbers(const std::vector<Paragraph>& paragraphs)
{
  std::vector<int> numbers;
  numbers.reserve(paragraphs.size());
  int counters[kListLevels] = {};
  for (const Paragraph& paragraph : paragraphs) {
    const ListFormat list = clamp_list(paragraph.list);
    if (list.kind == ListKind::None) {
      std::fill(std::begin(counters), std::end(counters), 0);
      numbers.push_back(0);
      continue;
    }
    if (list.kind == ListKind::Bullet) {
      numbers.push_back(0);
      continue;
    }
    // Paragraph counts cannot reach INT_MAX, but stay defined if they did.
    if (counters[list.level] < 2000000000)
      ++counters[list.level];
    for (int deeper = list.level + 1; deeper < kListLevels; ++deeper)
      counters[deeper] = 0;
    numbers.push_back(counters[list.level]);
  }
  return numbers;
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
  number = std::max(1, number);
  const int style = list.level % 3;
  std::string text;
  if (style == 1) {
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
    // Switching between bullets and numbers keeps the level and indents.
    paragraph.list = clamp_list(paragraph.list);
    paragraph.list.kind = kind;
    return;
  }
  paragraph.list.kind = kind;
  paragraph.list.level = 0;
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
  return a.paragraphs == b.paragraphs;
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
