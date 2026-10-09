/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"

#include <algorithm>

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

bool operator==(const Paragraph& a, const Paragraph& b)
{
  return a.heading == b.heading && a.indents == b.indents && a.runs == b.runs;
}

Indents clamp_indents(Indents indents)
{
  indents.left = std::max(0, std::min(kMaxIndent, indents.left));
  indents.right = std::max(0, std::min(kMaxIndent, indents.right));
  indents.first = std::max(-indents.left, std::min(kMaxIndent, indents.first));
  return indents;
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
