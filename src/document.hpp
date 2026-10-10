/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace writeit {

// A picture. `data` is the file bytes (png or jpeg); `path` is where a
// Markdown image came from, and may be empty for Insert > Picture. `width`
// and `height` are twips, 0 when the file does not say.
struct Image {
  std::string path;
  std::string alt;
  std::string type;
  std::string data;
  int width = 0;
  int height = 0;
};

constexpr std::size_t kMaxImageBytes = 8u * 1024u * 1024u;

struct Run {
  std::string text;
  std::string font = "Sans";
  // Points, in half-point steps as RTF's \fsN: 10.5 is \fs21.
  double size = 11;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  // 1-based index into Document::notes. 0 is not a footnote marker. The
  // marker's text is the number the page shows.
  int note = 0;
  // A picture in the run. Its text is empty. Two pictures are never merged,
  // even when the bytes match.
  std::optional<Image> image;
  // What was set directly rather than by the paragraph's style (kDirect*
  // bits). A style applied or edited leaves these alone. Not saved: a file
  // holds only values, and a reader takes what differs from the style as
  // direct, as Word does.
  unsigned direct = 0;
};

constexpr unsigned kDirectFont = 1;
constexpr unsigned kDirectSize = 2;
constexpr unsigned kDirectBold = 4;
constexpr unsigned kDirectItalic = 8;
constexpr unsigned kDirectUnderline = 16;

// Paragraph indents in twips (1/1440 inch), the unit RTF writes. `first` is
// the first line relative to `left`. A negative `first` is a hanging indent.
struct Indents {
  int left = 0;
  int right = 0;
  int first = 0;
};

// The largest indent Word and RTF accept: 22 inches.
constexpr int kMaxIndent = 31680;

// Page setup, in twips, as RTF's \paperw and \margl. The defaults are the A4
// page the window drew before Page Setup existed: 540 px is 11906 twips
// across, and the insets that were 42 px and 36 px at 100% (926 and 794
// twips). Columns are a section property; one column writes no \cols.
struct PageSetup {
  int paper_width = 11906;
  int paper_height = 16838;
  int margin_left = 926;
  int margin_right = 926;
  int margin_top = 794;
  int margin_bottom = 794;
  int columns = 1;
  int column_gap = 720;
  bool landscape = false;
};

constexpr PageSetup default_page()
{
  return {};
}

// Paper from about an inch to the indent ceiling. Margins stay inside the
// sheet and leave at least a quarter inch of text. The reader accepts up to
// twelve columns; the Columns dialog offers four.
constexpr int kMinPaperTwips = 1440;
constexpr int kMaxPaperTwips = kMaxIndent;
constexpr int kMinTextTwips = 360;
constexpr int kMaxColumns = 12;
constexpr int kMaxColumnGap = 2880;

constexpr int kMaxNotes = 200;
constexpr int kMaxTableRows = 32;
constexpr int kMaxTableColumns = 16;
constexpr int kMinCellTwips = 200;

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

// One cell of a table. `table` 0 is an ordinary paragraph. `widths` is one
// entry per column, in twips, shared by every cell of the table; empty means
// the file did not say, and a new table fills them from the text width.
struct Cell {
  int table = 0;
  int row = 0;
  int column = 0;
  int rows = 0;
  int columns = 0;
  std::vector<int> widths;
};

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
  // An empty paragraph's own character format, its paragraph mark's in Word:
  // the size its line is drawn at and typing there starts in. Its text is
  // empty. A paragraph with text takes its format from its runs, and this is
  // neither written nor compared then. None is the document default, as an
  // empty paragraph read from a file that gives it no format of its own.
  std::optional<Run> mark;
  // The paragraph format set directly (kDirect* bits below), as Run's.
  unsigned direct = 0;
  // A table cell, or table 0 for an ordinary paragraph. Several paragraphs
  // may share a row and column: a cell holds more than one paragraph.
  Cell cell;
  // A page break before this paragraph, RTF's \page.
  bool page_break = false;
};

constexpr unsigned kDirectLeft = 1;
constexpr unsigned kDirectRight = 2;
constexpr unsigned kDirectFirst = 4;
constexpr unsigned kDirectAlign = 8;

struct Document {
  std::vector<Paragraph> paragraphs;
  // The style sheet. Empty means the built-in sheet in Sans 11, which is
  // what a file without a \stylesheet reads as.
  std::vector<Style> styles;
  // Paper, margins, and columns. The default is omitted from the file, so a
  // document from before page setup still saves as the same bytes.
  PageSetup page;
  // The header and footer stories. Empty is omitted from the file.
  std::vector<Paragraph> header;
  std::vector<Paragraph> footer;
  // Footnotes, in the order their markers cite. notes[i] belongs to a run
  // whose note is i + 1.
  std::vector<std::vector<Paragraph>> notes;
};

// Same values and the same direct bits: runs that may be one run.
bool same_format(const Run& a, const Run& b);
// Same values, whatever was set directly: what a reader would see.
bool same_look(const Run& a, const Run& b);
// Two paragraphs' own formats (Paragraph::mark), which only empty paragraphs
// have, compared as same_look(): true when either has text.
bool same_mark(const Paragraph& a, const Paragraph& b);
bool operator==(const PageSetup& a, const PageSetup& b);
bool operator!=(const PageSetup& a, const PageSetup& b);
bool operator==(const Cell& a, const Cell& b);
bool operator!=(const Cell& a, const Cell& b);
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

// Paper, margins, and columns brought inside the limits above. A landscape
// flag does not swap the edges; the dialog and the RTF reader do that.
PageSetup clamp_page(PageSetup page);
// Paper, margins, and orientation are the defaults. Columns are separate:
// a two-column A4 page still has the default sheet.
bool page_metrics_default(const PageSetup& page);
// The text width between the margins, at least a quarter inch.
int page_text_twips(const PageSetup& page);

// Inserts `rows` by `columns` empty cells at `at` (clamped to the vector).
// Widths share `text_width`. False when the counts are out of range.
bool insert_table(std::vector<Paragraph>& paragraphs, size_t at, int rows, int columns,
                  int text_width);
// The paragraph is a cell. Row and column edits use the cell at `index`.
bool in_table(const std::vector<Paragraph>& paragraphs, size_t index);
// Insert or delete the caret cell's row or column. False when `index` is
// not a cell, or a delete would not change the table.
bool insert_table_row(std::vector<Paragraph>& paragraphs, size_t index);
bool delete_table_row(std::vector<Paragraph>& paragraphs, size_t index);
bool insert_table_column(std::vector<Paragraph>& paragraphs, size_t index);
bool delete_table_column(std::vector<Paragraph>& paragraphs, size_t index);

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
std::vector<Style> builtin_styles(const std::string& font, double size);
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
void adopt_sheet(Document& doc, const std::string& font, double size);
// Headings from before styles (M1 wrote \\outlinelevel on body-size text and
// showed it scaled): a document without a sheet whose paragraphs have
// outline levels adopts the built-in sheet and gives each heading paragraph
// in Normal its Heading 1-6 style, so headings keep their heading size. A
// document with a sheet, or without headings, is left alone.
void adopt_heading_styles(Document& doc, const std::string& font, double size);

Document blank_document(const std::string& font, double size);
Document plain_import(const std::string& text, const std::string& font, double size);
Document markdown_import(const std::string& text, const std::string& font, double size);
std::string markdown_export(const Document& doc);

// False when the text is not RTF. Straightforward files keep the paragraphs and
// character format this slice understands.
bool rtf_import(const std::string& text, Document& doc);
std::string rtf_export(const Document& doc);

// "png", "jpeg", or "" when the bytes are neither.
std::string image_type_of(const std::string& data);
// Reads `path` into `image.data` when it is a png or jpeg within
// kMaxImageBytes. Leaves path and alt alone. False when the file is missing
// or not a picture this slice embeds.
bool load_image_file(const std::string& path, Image& image);

}  // namespace writeit
