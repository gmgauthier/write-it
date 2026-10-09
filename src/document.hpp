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

// A named paragraph style, as Word 97's Format > Style: a character format
// and a paragraph format the paragraphs using it share. Paragraphs keep their
// own resolved formatting too; a style is applied to them, and an edit to a
// style is carried to them, attribute by attribute (see apply_style()).
struct Style {
  std::string name;
  // The style this one is built on, or empty. An edit to the base carries to
  // every attribute this style shares with it.
  std::string based_on;
  // The style Enter gives the next paragraph; empty for this one.
  std::string next;
  // Font, size, bold, italic and underline. The text is unused.
  Run format;
  Indents indents;
  Align align = Align::Left;
  // The outline level the style gives: 0 for body text, 1 through 6.
  int heading = 0;
};

constexpr const char* kNormalStyle = "Normal";
// Enough for any real document; a hostile file cannot grow the sheet further.
constexpr size_t kMaxStyles = 4096;
// Style names are capped at this many bytes of UTF-8.
constexpr size_t kMaxStyleName = 255;
// A style's largest size in points, Word's largest.
constexpr int kMaxStyleSize = 1638;

struct Paragraph {
  // 0 is body text. 1 through 6 are Markdown headings.
  int heading = 0;
  // The name of the paragraph's style. A name the sheet does not know is
  // treated as Normal.
  std::string style = kNormalStyle;
  Indents indents;
  Align align = Align::Left;
  ListFormat list;
  std::vector<Run> runs;
};

struct Document {
  std::vector<Paragraph> paragraphs;
  // The style sheet. Empty means the built-in sheet in Sans 11, which is
  // what a file without a \stylesheet reads as.
  std::vector<Style> styles;
};

bool same_format(const Run& a, const Run& b);
bool operator==(const Indents& a, const Indents& b);
bool operator!=(const Indents& a, const Indents& b);
bool operator==(const ListFormat& a, const ListFormat& b);
bool operator!=(const ListFormat& a, const ListFormat& b);
bool operator==(const Run& a, const Run& b);
bool operator==(const Paragraph& a, const Paragraph& b);
bool operator==(const Document& a, const Document& b);
bool operator==(const Style& a, const Style& b);
bool operator!=(const Style& a, const Style& b);

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

// Word 97's and AbiWord's basic set: Normal; Heading 1 through 6, one per
// Markdown heading level and outline level, each based on Normal with Normal
// next; Block Text, indented an inch each side as in Word 97; and Plain Text,
// in Monospace a point smaller, as Word 97's Plain Text is Courier New.
std::vector<Style> builtin_styles(const std::string& font, int size);
// The document's sheet: its own, or the built-in one in Sans 11.
const std::vector<Style>& style_sheet(const Document& doc);
// builtin_styles("Sans", 11), the sheet of a document without its own.
const std::vector<Style>& default_styles();
// A sheet holding every built-in style, Normal first: the built-ins a sheet
// lacks are added in the font and size of its Normal.
std::vector<Style> complete_sheet(std::vector<Style> sheet);
// By name, ignoring case for ASCII letters. Null when the sheet has none.
const Style* find_style(const std::vector<Style>& sheet, const std::string& name);
// A clean style name: valid UTF-8 without control characters, trimmed, and
// at most kMaxStyleName bytes. Empty when nothing is left.
std::string clean_style_name(const std::string& name);
// `wanted`, or "wanted (2)", "wanted (3)"... whichever the sheet lacks.
std::string unique_style_name(const std::vector<Style>& sheet, const std::string& wanted);
// The style Enter gives the paragraph after one in `name`.
std::string next_style(const std::vector<Style>& sheet, const std::string& name);
// Moves a run from one style to another: each attribute equal to `from`'s
// takes `to`'s, so direct formatting survives.
void restyle_run(Run& run, const Style& from, const Style& to);
// Gives paragraphs `first` through `last` the style `name`, each attribute
// that matched its old style taking the new one's: the runs' character
// format, the indents (a list item's indents belong to the list, so its own
// remembered indents move instead), the alignment and the outline level.
// False when the sheet has no such style.
bool apply_style(Document& doc, size_t first, size_t last, const std::string& name);
// Replaces the style `name` with `changed` and carries the change to the
// paragraphs using it and to the styles based on it, the same attribute by
// attribute way. A new name renames it everywhere. False, changing nothing,
// for an unknown style, an empty or taken name, renaming Normal, a base or
// next style the sheet lacks, or a base that would make a circle.
bool update_style(Document& doc, const std::string& name, const Style& changed);
// Adds a new style. False for an empty or taken name, an unknown base or
// next style, or a full sheet.
bool add_style(Document& doc, const Style& style);
// Gives a document without a sheet the built-in one, in the font and size
// of most of its text (font and size when it has none). The editor does this
// when such a document first takes a style.
void adopt_sheet(Document& doc, const std::string& font, int size);
// Headings from before styles (M1 wrote \\outlinelevel on body-size text and
// showed it scaled): a document without a sheet whose paragraphs have
// outline levels adopts the built-in sheet and gives each heading paragraph
// in Normal its Heading 1-6 style, so headings keep their heading size. A
// document with a sheet, or without headings, is left alone.
void adopt_heading_styles(Document& doc, const std::string& font, int size);

Document blank_document(const std::string& font, int size);
Document plain_import(const std::string& text, const std::string& font, int size);
Document markdown_import(const std::string& text, const std::string& font, int size);
std::string markdown_export(const Document& doc);

// False when the text is not RTF. Straightforward files keep the paragraphs and
// character format this slice understands.
bool rtf_import(const std::string& text, Document& doc);
std::string rtf_export(const Document& doc);

}  // namespace writeit
