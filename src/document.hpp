/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace writeit {

struct Run {
  std::string text;
  std::string font = "Sans";
  int size = 11;
  bool bold = false;
  bool italic = false;
  bool underline = false;
};

// Paragraph indents in twips (1/1440 inch), the unit RTF writes. `first` is
// the first line relative to `left`. A negative `first` is a hanging indent.
struct Indents {
  int left = 0;
  int right = 0;
  int first = 0;
};

// The largest indent Word and RTF accept: 22 inches.
constexpr int kMaxIndent = 31680;

struct Paragraph {
  // 0 is body text. 1 through 6 are Markdown headings.
  int heading = 0;
  Indents indents;
  std::vector<Run> runs;
};

struct Document {
  std::vector<Paragraph> paragraphs;
};

bool same_format(const Run& a, const Run& b);
bool operator==(const Indents& a, const Indents& b);
bool operator!=(const Indents& a, const Indents& b);
bool operator==(const Run& a, const Run& b);
bool operator==(const Paragraph& a, const Paragraph& b);
bool operator==(const Document& a, const Document& b);

// Left and right stay between 0 and kMaxIndent. The first line may hang back
// to the left margin and no further, and may indent up to kMaxIndent.
Indents clamp_indents(Indents indents);

Document blank_document(const std::string& font, int size);
Document plain_import(const std::string& text, const std::string& font, int size);
Document markdown_import(const std::string& text, const std::string& font, int size);
std::string markdown_export(const Document& doc);

// False when the text is not RTF. Straightforward files keep the paragraphs and
// character format this slice understands.
bool rtf_import(const std::string& text, Document& doc);
std::string rtf_export(const Document& doc);

}  // namespace writeit
