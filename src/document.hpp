/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace writeit {

struct Run {
  std::string text;
  std::string font = "Sans";
  // Points, in half-point steps as RTF's \fsN: 10.5 is \fs21.
  double size = 11;
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

// Word 97's Align Left, Center, Align Right and Justify. RTF's \ql, \qc, \qr
// and \qj; Justify is GTK's JUSTIFY_FILL.
enum class Align { Left, Center, Right, Justify };

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
  // The number the item's list level starts at, as Word's \levelstartat: 0
  // through kMaxListStart. A level takes its first item's start, at the
  // start of the list and again under each new parent. Bullets have none.
  int start = 1;
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
// Word's largest \levelstartat.
constexpr int kMaxListStart = 32767;
// Word's list indents: half an inch per level, the label hanging a quarter inch.
constexpr int kListStep = 720;
constexpr int kListHang = 360;
// Word 97's default tab stops: every half inch from the left margin.
constexpr int kDefaultTab = 720;

struct Paragraph {
  // 0 is body text. 1 through 6 are Markdown headings.
  int heading = 0;
  Indents indents;
  Align align = Align::Left;
  ListFormat list;
  std::vector<Run> runs;
  // An empty paragraph's own character format, its paragraph mark's in Word:
  // the size its line is drawn at and typing there starts in. Its text is
  // empty. A paragraph with text takes its format from its runs, and this is
  // neither written nor compared then. None is the document default, as an
  // empty paragraph read from a file that gives it no format of its own.
  std::optional<Run> mark;
};

struct Document {
  std::vector<Paragraph> paragraphs;
};

bool same_format(const Run& a, const Run& b);
// Two paragraphs' own formats (Paragraph::mark), which only empty paragraphs
// have: true when either has text.
bool same_mark(const Paragraph& a, const Paragraph& b);
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
// Where a list item's label starts, in pixels, in the coordinates of the
// arguments. Left-aligned items, and any alignment but Center and Right
// (justified text starts at the indent too), keep it at the first-line
// indent (`hang_x`, left + first) as before. Centred and right-aligned text
// moves away from the indent, so the label sits just before the first
// line's text (`text_x`): `space` before it, the paragraph's own room for
// the label (list_label_space() in pixels), or the label's width and `gap`
// when the label is wider than that. Never left of 0.
//
// GTK centres a list item's first line between where its text starts
// (list_text_start()) and the right indent. With the label `space` before
// the text, label and text are centred together, as one unit, between the
// first-line indent and the right indent, as in Word 97, and a
// right-aligned item's label and text end at the right indent.
int list_label_x(Align align, int hang_x, int text_x, int label_width, int space, int gap);
// Where a centred list item's text must be centred from, in pixels, so its
// label and first line centre as one unit between the first-line indent
// (`hang_x`) and the right indent, as in Word 97: the label's room before
// the text past the first-line indent. GTK centres every line of a
// paragraph between its left margin and the right indent (Pango ignores the
// first-line indent there), so the editor sets this as the paragraph's left
// margin on screen only, never in the document; wrapped lines move with it.
// -1 for anything not centred.
int list_centre_from(Align align, int hang_x, int label_width, int space, int gap);
// Where a list item's first line of text starts, in twips from the page
// margin: the left indent when the label hangs in front of it, else a
// standard hang past the label (left + first + kListHang), as when the
// first line does not hang or hangs less than that.
int list_text_start(const Indents& indents);
// The room between where a list item's label starts and where its text
// does, in twips: the paragraph's own hang (list_text_start minus left +
// first), at least the standard hang.
int list_label_space(const Indents& indents);
// Format > Bullets and Format > Numbering on the selected paragraphs. When
// every one already has `kind` the list comes off; otherwise every one takes
// it. A paragraph joining a list hangs its label in front of its text; one
// leaving gets back the indents it had before it joined (ListFormat::own).
// Returns the kind the paragraphs have.
ListKind toggle_list(std::vector<Paragraph>& paragraphs, ListKind kind);
// Moves a list item to another level, and its indents with it.
void set_list_level(Paragraph& paragraph, int level);

Document blank_document(const std::string& font, double size);
Document plain_import(const std::string& text, const std::string& font, double size);
Document markdown_import(const std::string& text, const std::string& font, double size);
std::string markdown_export(const Document& doc);

// False when the text is not RTF. Straightforward files keep the paragraphs and
// character format this slice understands.
bool rtf_import(const std::string& text, Document& doc);
std::string rtf_export(const Document& doc);

}  // namespace writeit
