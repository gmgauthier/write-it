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

// Word 97's Align Left, Center and Align Right. There is no justified; RTF's
// \qj reads as left.
enum class Align { Left, Center, Right };

// A bulleted or numbered list item. The list sits on top of the indents: the
// label hangs in the first-line indent and the text starts at the left one.
// Numbers are not stored; list_numbers() works them out from the paragraphs.
enum class ListKind { None, Bullet, Number };

struct ListFormat {
  ListFormat() = default;
  ListFormat(ListKind kind_in, int level_in)
      : kind(kind_in),
        level(level_in)
  {
  }

  ListKind kind = ListKind::None;
  // 0 through kListLevels - 1, as RTF's \ilvl.
  int level = 0;
  // Which numbered list the item counts in, as Word's \ls: a label, not a
  // count. 0 continues the numbered list above (the first list when none is
  // above). Items keep counting in their list past plain paragraphs, bullets
  // and other lists; a new list starts again at 1. Bullets have none.
  int list = 0;
  // The paragraph's own indents from before it joined the list, which
  // leaving the list gives back. Editing memory, not document content: it is
  // not written to the file and not part of equality. A list item read from
  // a file has none, and leaving takes off the list's indents instead.
  bool has_own = false;
  Indents own;
};

constexpr int kListLevels = 9;
// The most numbered lists a document holds; items of later lists join the
// last one. Below the RTF reader's 4096 list definitions.
constexpr int kMaxLists = 4000;
// Word's list indents: half an inch per level, the label hanging a quarter inch.
constexpr int kListStep = 720;
constexpr int kListHang = 360;

struct Paragraph {
  // 0 is body text. 1 through 6 are Markdown headings.
  int heading = 0;
  Indents indents;
  Align align = Align::Left;
  ListFormat list;
  std::vector<Run> runs;
};

struct Document {
  std::vector<Paragraph> paragraphs;
};

bool same_format(const Run& a, const Run& b);
bool operator==(const Indents& a, const Indents& b);
bool operator!=(const Indents& a, const Indents& b);
bool operator==(const ListFormat& a, const ListFormat& b);
bool operator!=(const ListFormat& a, const ListFormat& b);
bool operator==(const Run& a, const Run& b);
bool operator==(const Paragraph& a, const Paragraph& b);
bool operator==(const Document& a, const Document& b);

// Left and right stay between 0 and kMaxIndent. The first line may hang back
// to the left margin and no further, and may indent up to kMaxIndent.
Indents clamp_indents(Indents indents);

// The Paragraph dialog's check, as Word 97 does it: false when the first
// line would start left of the left margin (a hanging indent larger than
// Left). The dialog refuses such a choice rather than clamping it.
bool indents_fit(const Indents& indents);

// A level outside 0 through 8 moves to the nearer end. No list has no level.
ListFormat clamp_list(ListFormat list);
// The indents a new list item takes at a level.
Indents list_indents(int level);
// Each paragraph's number: 0 for a bullet or plain paragraph. Numbers count
// up per level, a numbered item restarts the levels below it, and a plain
// paragraph restarts them all. Bullets neither count nor interrupt.
std::vector<int> list_numbers(const std::vector<Paragraph>& paragraphs);
// Each paragraph's numbered list: 1 for the first list the document
// numbers, 2 for the next, and so on, in order of first appearance; 0 for a
// bullet or a plain paragraph. At most kMaxLists.
std::vector<int> list_ids(const std::vector<Paragraph>& paragraphs);
// Writes list_ids() back into the paragraphs: the same lists, labelled 1 up.
void canonical_lists(std::vector<Paragraph>& paragraphs);
// Word's Restart Numbering: the numbered item at `index` and the rest of its
// list become a new list, starting again at 1. False, changing nothing,
// when the item is not numbered or its list already starts there.
bool restart_numbering(std::vector<Paragraph>& paragraphs, size_t index);
// Word's Continue Previous List: the numbered item at `index` and the rest
// of its list join the nearest numbered list above that is another list.
// False, changing nothing, when there is none.
bool continue_numbering(std::vector<Paragraph>& paragraphs, size_t index);
// The label in front of an item: a bullet by level, or "1.", "a.", "i." by
// level for a number. Empty for no list.
std::string list_label(const ListFormat& list, int number);
// Format > Bullets and Format > Numbering on the selected paragraphs. When
// every one already has `kind` the list comes off; otherwise every one takes
// it. A paragraph joining a list hangs its label in front of its text; one
// leaving gets back the indents it had before it joined (ListFormat::own).
// Returns the kind the paragraphs have.
ListKind toggle_list(std::vector<Paragraph>& paragraphs, ListKind kind);
// Moves a list item to another level, and its indents with it.
void set_list_level(Paragraph& paragraph, int level);

Document blank_document(const std::string& font, int size);
Document plain_import(const std::string& text, const std::string& font, int size);
Document markdown_import(const std::string& text, const std::string& font, int size);
std::string markdown_export(const Document& doc);

// False when the text is not RTF. Straightforward files keep the paragraphs and
// character format this slice understands.
bool rtf_import(const std::string& text, Document& doc);
std::string rtf_export(const Document& doc);

}  // namespace writeit
