/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <glibmm/refptr.h>

#include <optional>
#include <string>
#include <vector>

namespace Gdk {
class Pixbuf;
}

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

// Word 97's font colours, in the order of Word's default colour table.
// 0 is automatic for a font colour and none for a highlight. Highlight uses
// this same index (\highlightN), not a colour-table lookup.
enum class Ink : int {
  Auto = 0,
  Black = 1,
  Blue = 2,
  Cyan = 3,
  Green = 4,
  Magenta = 5,
  Red = 6,
  Yellow = 7,
  White = 8,
  DarkBlue = 9,
  Teal = 10,
  DarkGreen = 11,
  Violet = 12,
  DarkRed = 13,
  DarkYellow = 14,
  Gray50 = 15,
  Gray25 = 16,
};

// Superscript and subscript do not apply together.
enum class Script : int { None = 0, Super = 1, Sub = 2 };

struct Rgb {
  int red = 0;
  int green = 0;
  int blue = 0;
};

// Word 97's names. Index 0 is "Automatic" for a font colour and "None" for a
// highlight. An index outside 0–16 is named as automatic.
const char* ink_name(Ink ink, bool highlight);
// Red, green, and blue. Automatic is 0, 0, 0, which is also black.
Rgb ink_rgb(Ink ink);
// The palette colour with this red, green, and blue, or the nearest of the
// sixteen. A tie keeps the lower index. Automatic is not a match.
Ink ink_from_rgb(int red, int green, int blue);
// An index outside 0–16 is automatic.
Ink ink_clamp(int index);
// #RRGGBB of ink_rgb().
std::string ink_hex(Ink ink);

struct Run {
  std::string text;
  std::string font = "Sans";
  // Points, in half-point steps as RTF's \fsN: 10.5 is \fs21.
  double size = 11;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  // One font colour, one highlight, strikethrough, and one of superscript,
  // subscript, or neither. Highlight's Auto is none.
  Ink color = Ink::Auto;
  Ink highlight = Ink::Auto;
  bool strike = false;
  Script script = Script::None;
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
constexpr unsigned kDirectColor = 32;
constexpr unsigned kDirectHighlight = 64;
constexpr unsigned kDirectStrike = 128;
// One bit: superscript and subscript do not apply together.
constexpr unsigned kDirectScript = 256;

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

// Word 97's line spacing. Single, 1.5 and Double ignore `value`. At least and
// Exactly store twips (0 through kMaxIndent, which is 0 pt through 1584 pt).
// Multiple stores a line count in 240ths of a line: 240 is one line, and 132
// lines is kMaxIndent.
enum class LineRule { Single, OneHalf, Double, AtLeast, Exactly, Multiple };

struct Spacing {
  int before = 0;
  int after = 0;
  LineRule rule = LineRule::Single;
  int value = 240;
};

// Named rules ignore value. At least, Exactly and Multiple compare it.
bool same_spacing_line(const Spacing& a, const Spacing& b);
Spacing clamp_spacing(Spacing spacing);
// \sl and \slmult as read. sl 0 is single, including \sl0\slmult1. A negative
// sl is exactly. A positive sl with slmult is 240, 360, 480 or multiple;
// without it, at least. Exactly 0, at least 0 and multiple 0 are told apart
// by the reader before this is called: \sl-0, \sl0\slmult0, and \slmult1\sl0.
void apply_line_spacing(Spacing& spacing, int sl, int slmult);
// The \sl and \slmult that write this rule back. Single is 0 and 0, which the
// writer omits unless a style has to be turned off. Exactly 0 is negative 0.
void line_spacing_raw(const Spacing& spacing, int& sl, int& slmult);

// Word 97's border line styles, in the order Borders and Shading lists them
// after None. None is the absence of a side, not a stroke.
enum class BorderStyle : int {
  None = 0,
  Single = 1,
  Thick = 2,
  Double = 3,
  Dotted = 4,
  Dashed = 5,
  Hairline = 6,
  DotDash = 7,
  DotDotDash = 8,
  Triple = 9,
  Inset = 10,
  Outset = 11,
  Emboss = 12,
  Engrave = 13,
};

// One edge. Width and space are twips. Colour Auto is the window text, which
// is black. A side that is None ignores the rest.
struct BorderSide {
  BorderStyle style = BorderStyle::None;
  int width = 0;
  Ink color = Ink::Auto;
  int space = 0;
  bool shadow = false;
};

// The four edges Word 97's paragraph and cell borders store. Between and bar
// (\brdrbtw, \brdrbar) are not.
struct Borders {
  BorderSide top;
  BorderSide left;
  BorderSide bottom;
  BorderSide right;
};

// Shading as RTF writes it. `pattern` is \shadingN: hundredths of a percent,
// 0 clear through 10000 solid. Foreground is the pattern colour (\cfpat) and
// background is the fill (\cbpat). Auto is none. Decorative patterns
// (\bghoriz and the rest) are not stored.
struct Shading {
  int pattern = 0;
  Ink foreground = Ink::Auto;
  Ink background = Ink::Auto;
};

// Turning a side on starts here: a single line, 1/2 pt, automatic.
constexpr int kDefaultBorderWidth = 10;
constexpr int kMaxShading = 10000;
// Word 97's width list, 1/4 pt through 6 pt.
constexpr int kBorderWidths[] = {5, 10, 15, 20, 30, 45, 60, 90, 120};
constexpr int kBorderWidthCount = 9;

// Word 97's pattern list: Clear, Solid, then the percent steps.
struct ShadeStep {
  int pattern;
  const char* name;
};
constexpr ShadeStep kShadeSteps[] = {
    {0, "Clear"},    {10000, "Solid (100%)"}, {500, "5%"},     {1000, "10%"}, {1250, "12.5%"},
    {1500, "15%"},   {2000, "20%"},           {2500, "25%"},   {3000, "30%"}, {3500, "35%"},
    {3750, "37.5%"}, {4000, "40%"},           {4500, "45%"},   {5000, "50%"}, {5500, "55%"},
    {6000, "60%"},   {6250, "62.5%"},         {6500, "65%"},   {7000, "70%"}, {7500, "75%"},
    {8000, "80%"},   {8500, "85%"},           {8750, "87.5%"}, {9000, "90%"}, {9500, "95%"},
};
constexpr int kShadeStepCount = 25;

const char* border_style_name(BorderStyle style);
BorderStyle clamp_border_style(int style);
BorderSide clamp_side(BorderSide side);
Borders clamp_borders(Borders borders);
Shading clamp_shading(Shading shading);
bool side_plain(const BorderSide& side);
bool borders_plain(const Borders& borders);
bool shading_plain(const Shading& shading);
// The colour this shading paints. False when it paints nothing (clear, and
// both colours automatic). A percent mixes the pattern colour over the fill;
// an automatic pattern colour is black, and solid with an automatic pattern
// colour is the fill.
bool shading_color(const Shading& shading, Rgb& out);
bool operator==(const BorderSide& a, const BorderSide& b);
bool operator!=(const BorderSide& a, const BorderSide& b);
bool operator==(const Borders& a, const Borders& b);
bool operator!=(const Borders& a, const Borders& b);
bool operator==(const Shading& a, const Shading& b);
bool operator!=(const Shading& a, const Shading& b);

// A box of one style. 3-D is inset on the top and left, outset on the bottom
// and right, which is how Word 97 writes it so the file round-trips.
inline Borders border_box(BorderStyle style, int width, Ink color, int space = 0,
                          bool shadow = false)
{
  BorderSide side;
  side.style = style;
  side.width = width;
  side.color = color;
  side.space = space;
  side.shadow = shadow;
  Borders borders;
  borders.top = side;
  borders.left = side;
  borders.bottom = side;
  borders.right = side;
  return borders;
}

inline Borders border_threed(int width, Ink color)
{
  Borders borders;
  borders.top.style = BorderStyle::Inset;
  borders.left.style = BorderStyle::Inset;
  borders.bottom.style = BorderStyle::Outset;
  borders.right.style = BorderStyle::Outset;
  borders.top.width = borders.left.width = borders.bottom.width = borders.right.width = width;
  borders.top.color = borders.left.color = borders.bottom.color = borders.right.color = color;
  return borders;
}

inline BorderSide& border_at(Borders& borders, int edge)
{
  if (edge == 1)
    return borders.left;
  if (edge == 2)
    return borders.bottom;
  if (edge == 3)
    return borders.right;
  return borders.top;
}

inline const BorderSide& border_at(const Borders& borders, int edge)
{
  if (edge == 1)
    return borders.left;
  if (edge == 2)
    return borders.bottom;
  if (edge == 3)
    return borders.right;
  return borders.top;
}

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
  // Font, size, bold, italic, underline, colour, highlight, strikethrough,
  // and script. The text is unused.
  Run format;
  Indents indents;
  Align align = Align::Left;
  Spacing spacing;
  Borders borders;
  Shading shading;
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
  // False keeps the 1.0 frame and writes no cell borders, so a file from
  // before this milestone is unchanged. True stores the edges, including
  // every side none, which writes \brdrnone and draws no frame.
  bool borders_set = false;
  Borders borders;
  Shading shading;
};

struct Paragraph {
  // 0 is body text. 1 through 6 are Markdown headings.
  int heading = 0;
  // The name of the paragraph's style. A name the sheet does not know is
  // treated as Normal.
  std::string style = kNormalStyle;
  Indents indents;
  Align align = Align::Left;
  Spacing spacing;
  Borders borders;
  Shading shading;
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
  // Half-points of the font the line spacing is drawn against. Display
  // memory: it is not written and not compared. 0 means work it out from
  // the runs, the paragraph mark, or 11 pt.
  int basis = 0;
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
constexpr unsigned kDirectBefore = 16;
constexpr unsigned kDirectAfter = 32;
// The line rule and its value, together.
constexpr unsigned kDirectLine = 64;
constexpr unsigned kDirectBorders = 128;
constexpr unsigned kDirectShading = 256;

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
bool operator==(const Spacing& a, const Spacing& b);
bool operator!=(const Spacing& a, const Spacing& b);
bool operator==(const ListFormat& a, const ListFormat& b);
bool operator!=(const ListFormat& a, const ListFormat& b);
bool operator==(const Run& a, const Run& b);
bool operator==(const Paragraph& a, const Paragraph& b);
bool operator==(const Document& a, const Document& b);
bool operator==(const Style& a, const Style& b);
bool operator!=(const Style& a, const Style& b);

// The first run's size, else the paragraph mark's, else a stored basis of at
// least 1 pt, else 11 pt. Half-points, as RTF's \fsN.
int basis_half_points(const Paragraph& paragraph);

// Left and right stay between 0 and kMaxIndent. The first line may hang back
// to the left margin and no further, and may indent up to kMaxIndent.
// When text_width is zero or more, the indents also leave kMinTextTwips of
// that text area. A negative text_width skips that fit.
Indents clamp_indents(Indents indents, int text_width = -1);

// Paper, margins, and columns brought inside the limits above. A landscape
// flag does not swap the edges; the dialog and the RTF reader do that.
PageSetup clamp_page(PageSetup page);
// Paper, margins, and orientation are the defaults. Columns are separate:
// a two-column A4 page still has the default sheet.
bool page_metrics_default(const PageSetup& page);
// The text width between the margins, at least a quarter inch.
int page_text_twips(const PageSetup& page);
// How wide a table may be: the text width, or out through the right margin
// to the page edge, whichever is wider.
int table_width_limit(const PageSetup& page);

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
// Scales `widths` so the columns add up to `total`, each at least 200 twips.
// `total` is brought inside the columns' minimum and `limit`. The last column
// takes the remainder. False when `limit` cannot hold the columns.
bool fit_table_widths(std::vector<int>& widths, int total, int limit);
// Sets one column. The others stay when the table still fits in `limit`;
// otherwise they scale into the room that is left. A one-column table's
// width is the table width. False when the column is out of range or `limit`
// cannot hold the columns.
bool set_table_column_width(std::vector<int>& widths, int column, int width, int limit);
// Moves the right edge of `column` by `delta` twips. An interior edge keeps
// the table's width and leaves both columns at least 200 twips. The last
// column's edge changes the table width, within `limit`. False when `column`
// is out of range, or an interior pair cannot hold two columns. A zero delta
// leaves `widths` as it is and returns true.
bool move_table_edge(std::vector<int>& widths, int column, int delta, int limit);
// Writes `widths` onto every paragraph of the table at `index`. False when
// `index` is not a cell, or `widths` is not one entry per column.
bool apply_table_widths(std::vector<Paragraph>& paragraphs, size_t index,
                        const std::vector<int>& widths);

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
// Characters of a run in the body buffer. A picture is one character.
int chars_of(const Run& run);
int chars_of(const Paragraph& paragraph);
// Buffer offset where paragraph `index` starts. A table is one anchor
// character; its cells share that offset. A newline after the anchor, when
// another paragraph follows, is a separator. One past the last paragraph
// counts a newline the last paragraph does not store.
int chars_before(const std::vector<Paragraph>& paragraphs, size_t index);
// Body offset of the marker for `note_index` (1-based, as Run::note). A
// marker in ordinary text is that run's offset. A note whose only marker is
// inside a table is the table anchor's offset: the cells share it, and the
// sheet that holds the anchor is the sheet that shows the note. -1 when no
// run cites the note.
int note_marker_offset(const std::vector<Paragraph>& paragraphs, int note_index);
// Insert > Page Break at a body caret, counted the same way as chars_before().
// Returns the caret's new offset, or -1 when that point already starts a
// sheet. A caret past a table's anchor, clamped into a cell, appends a body
// paragraph. A break at the end of the paragraph before a table inserts an
// empty body paragraph in front of the table. The cell itself is not flagged.
int break_page(Document& doc, int caret_offset);
// Gives one paragraph the style `to`. An attribute whose direct bit is unset
// takes the style's value and the bit stays unset, even when the old value
// differed from the paragraph's previous style. A set bit is left alone.
// Body text uses apply_style(), which also keeps a value that merely differs.
void take_paragraph_style(Paragraph& paragraph, const Style& to);
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

// A blank line in a note splits it, and the new half has no marker. Markers
// after the changed notes move by the change in note count. A marker that
// still names the note that split stays. `doc.notes` is already the edited
// list; `before_notes` is the list it replaced. The same count changes nothing.
void retarget_note_markers(Document& doc, const std::vector<std::vector<Paragraph>>& before_notes);

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
// PNG bytes of a pixbuf a paste left in the buffer. Empty when `pix` is
// null or will not encode. Does not invent width or height.
Image image_from_pixbuf(const Glib::RefPtr<Gdk::Pixbuf>& pix);

// True when a body rebuild has to be one custom undo step. Either document
// has a table, or a picture with bytes, including a picture in a cell. A
// picture with no bytes does not count, and neither does plain text.
bool document_needs_custom_undo(const Document& before, const Document& after);

// Where each note starts in the joined note text, as character offsets: 0,
// then just after every blank line. GTK counts characters, not UTF-8 bytes.
std::vector<int> note_break_offsets(const std::string& utf8);

// What one paragraph contributes to a story view. An empty paragraph, or one
// that is only a picture, is a single space, so the join is not a blank line.
// Whitespace-only text is kept. A picture beside text contributes the text.
std::string paragraph_plain(const Paragraph& paragraph);
// Paragraphs joined by one newline. An empty story is empty.
std::string story_plain_text(const std::vector<Paragraph>& story);

// Gives one paragraph the style `to`, keeping a value that differs from the
// style it is in even when the direct bit is unset. A file stores that
// difference with no bit. take_paragraph_style() does not.
void restyle_paragraph_kept(Paragraph& paragraph, const std::vector<Style>& sheet, const Style& to);

// Notes whose markers were deleted. remap[0] is 0. remap[old] is the new
// 1-based number, or 0 when that note had no marker in the body (cells live
// there). `notes` is the kept stories, in the new order.
struct NoteDrop {
  std::vector<std::vector<Paragraph>> notes;
  std::vector<int> remap;
  bool changed = false;
};

NoteDrop drop_unmarked_notes(const Document& doc);
// Writes the new numbers onto body markers. A marker whose text is empty or
// the old number takes the new number's text. A dropped note's marker loses
// its note.
void apply_note_remap(Document& doc, const std::vector<int>& remap);

// Body offsets of paragraphs whose list id changed between `before` and
// `after`. A cell is skipped: a table is one anchor, and tagging each cell
// as its own paragraph lands the new list on the wrong body paragraph.
struct ListRetag {
  int offset = 0;
  size_t index = 0;
};

std::vector<ListRetag> list_retag_offsets(const std::vector<Paragraph>& before,
                                          const std::vector<Paragraph>& after);

// A header, footer, or note taking the keyboard leaves the cell that was
// being edited. The body taking it does not: a file dialog and the Table
// menu both focus away from the cell and still need it.
bool story_focus_clears_cell(bool header, bool footer, bool note);

}  // namespace writeit
