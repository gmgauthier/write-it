/* SPDX-License-Identifier: Unlicense */

#include "document.hpp"
#include "font_sizes.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace writeit {
namespace {

const uint32_t kReplacement = 0xFFFD;

bool is_scalar(uint32_t cp)
{
  return cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF);
}

// Never writes anything but valid UTF-8: a value that is not a Unicode scalar
// value becomes U+FFFD.
void append_utf8(std::string& out, uint32_t cp)
{
  if (!is_scalar(cp))
    cp = kReplacement;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Decodes one well-formed UTF-8 sequence at i. On success advances i and
// returns true; on anything malformed (truncated, bad continuation, overlong,
// surrogate, past U+10FFFF) leaves i alone and returns false.
bool decode_utf8_strict(const std::string& text, size_t& i, uint32_t& cp)
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
  if (cp < min || !is_scalar(cp))
    return false;
  i += len;
  return true;
}

// Lenient: a malformed byte decodes as U+FFFD and is consumed on its own.
uint32_t decode_utf8(const std::string& text, size_t& i)
{
  uint32_t cp = 0;
  if (decode_utf8_strict(text, i, cp))
    return cp;
  ++i;
  return kReplacement;
}

// The safety net: anything that is not valid UTF-8 is replaced, so nothing
// the reader produces can upset GTK.
std::string valid_utf8(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    const size_t begin = i;
    uint32_t cp = 0;
    if (decode_utf8_strict(text, i, cp)) {
      out.append(text, begin, i - begin);
    } else {
      append_utf8(out, kReplacement);
      ++i;
    }
  }
  return out;
}

// C0 controls, DEL and C1 controls. Tab, newline and carriage return are
// controls too; callers that give them meaning check for them first.
bool is_control(uint32_t cp)
{
  return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

// The reader's last word on a string: valid UTF-8 with no control character
// but, where allowed, tab.
std::string clean_text(const std::string& text, bool keep_tab)
{
  const std::string valid = valid_utf8(text);
  std::string out;
  out.reserve(valid.size());
  size_t i = 0;
  while (i < valid.size()) {
    const size_t begin = i;
    const uint32_t cp = decode_utf8(valid, i);
    if (is_control(cp) && !(keep_tab && cp == '\t'))
      continue;
    out.append(valid, begin, i - begin);
  }
  return out;
}

// RTF text for a run or a font name. No raw control character is written:
// in text a tab is \tab and a newline (LF, CR or CR LF) is \line; in a font
// name, and for every other control, the character is dropped.
std::string escape_rtf(const std::string& text, bool font_name = false)
{
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\\' || c == '{' || c == '}') {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    if (c == '\t' && !font_name) {
      out += "\\tab ";
      ++i;
      continue;
    }
    if ((c == '\n' || c == '\r') && !font_name) {
      out += "\\line ";
      ++i;
      if (c == '\r' && i < text.size() && text[i] == '\n')
        ++i;
      continue;
    }
    if (c < 0x20 || c == 0x7F) {
      ++i;
      continue;
    }
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    const uint32_t cp = decode_utf8(text, i);
    if (is_control(cp))
      continue;
    auto unit = [&](uint32_t value) {
      const int n = value >= 32768 ? static_cast<int>(value) - 65536 : static_cast<int>(value);
      out += "\\u" + std::to_string(n) + "?";
    };
    if (cp <= 0xFFFF) {
      unit(cp);
    } else {
      const uint32_t u = cp - 0x10000;
      unit(0xD800 + ((u >> 10) & 0x3FF));
      unit(0xDC00 + (u & 0x3FF));
    }
  }
  return out;
}

const char* font_family(const std::string& name)
{
  std::string lower = name;
  for (char& c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower.find("mono") != std::string::npos || lower.find("courier") != std::string::npos ||
      lower.find("consol") != std::string::npos)
    return "fmodern";
  if (lower.find("times") != std::string::npos || lower.find("roman") != std::string::npos ||
      lower.find("serif") != std::string::npos || lower.find("georgia") != std::string::npos)
    return "froman";
  return "fswiss";
}

std::string trim(const std::string& text)
{
  size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
    ++begin;
  size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
    --end;
  return text.substr(begin, end - begin);
}

uint32_t cp1252(unsigned char byte)
{
  static const uint32_t kHigh[32] = {
      0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
      0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  if (byte >= 0x80 && byte <= 0x9F)
    return kHigh[byte - 0x80];
  return byte;
}

bool skip_destination(const std::string& word)
{
  return word == "stylesheet" || word == "info" || word == "colortbl" || word == "pict" ||
         word == "object" || word == "footer" || word == "footerf" || word == "header" ||
         word == "headerf" || word == "footnote" || word == "fldinst" || word == "fldrslt" ||
         word == "field" || word == "xmlnstbl" || word == "generator" || word == "listtable" ||
         word == "listoverridetable" || word == "rsidtbl" || word == "themedata" ||
         word == "latentstyles" || word == "colorschememapping" || word == "datastore" ||
         word == "nonshppict" || word == "shppict" || word == "background" || word == "pntext" ||
         word == "listtext" || word == "revtbl" || word == "xmlopen" || word == "xmlclose";
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

// The list words this reader takes from a paragraph, resolved against the
// list tables once the whole file is read.
struct ListMarks {
  int ls = 0;     // \ls: an index into the list override table, 1 and up.
  int ilvl = 0;   // \ilvl
  ListFormat pn;  // Word 97's {\*\pn ...}
};

// Which list table a group is inside.
enum class ListTable { None, Lists, Overrides };

struct State {
  int font = 0;
  int half_points = 22;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  int heading = 0;
  // Paragraph properties. They belong to the paragraph that the next \par ends.
  Indents indents;
  Align align = Align::Left;
  ListMarks marks;
  // The paragraph's style: an index into the style sheet.
  int style = 0;
  bool ignore = false;
  // Inside {\stylesheet ...}, and inside one of its entries.
  bool in_sheet = false;
  bool in_style = false;
  // Inside {\*\pn ...}, {\*\listtable ...} or {\*\listoverridetable ...}:
  // the text is ignored, but these words are still read.
  bool in_pn = false;
  ListTable table = ListTable::None;
  bool in_list = false;
  bool in_override = false;
  bool in_fonttbl = false;
  // Inside a font entry, a group that is not the name: \*\panose, \falt.
  bool font_skip = false;
  bool pending_dest = false;
  int uc = 1;
};

// One {\s...} entry of the style sheet as read: only what it sets.
// The level a built-in heading style's name stands for, else 0.
int builtin_heading(const std::string& name)
{
  if (name.size() == 9 && name.compare(0, 8, "Heading ") == 0 && name[8] >= '1' && name[8] <= '6')
    return name[8] - '0';
  return 0;
}

// The level a "heading N" name stands for, in any case, else `otherwise`.
int name_heading(const std::string& name, int otherwise)
{
  if (name.size() != 9 || name[8] < '1' || name[8] > '6')
    return otherwise;
  static const char kHeading[] = "heading ";
  for (size_t i = 0; i < 8; ++i) {
    if (std::tolower(static_cast<unsigned char>(name[i])) != kHeading[i])
      return otherwise;
  }
  return name[8] - '0';
}

struct StyleEntry {
  int index = 0;
  bool paragraph = true;
  int based_on = -1;
  int next = -1;
  std::string name;
  bool name_done = false;
  std::optional<int> font;
  std::optional<int> half_points;
  std::optional<bool> bold;
  std::optional<bool> italic;
  std::optional<bool> underline;
  std::optional<int> left;
  std::optional<int> right;
  std::optional<int> first;
  std::optional<Align> align;
  std::optional<int> heading;
};
// A style resolved against its bases, in the reader's own terms.
struct ReadStyle {
  int font = 0;
  int half_points = 22;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  Indents indents;
  Align align = Align::Left;
  int heading = 0;
};

class Reader {
 public:
  explicit Reader(const std::string& text)
      : text_(text)
  {
  }

  bool parse(Document& doc)
  {
    if (text_.find("{\\rtf") == std::string::npos)
      return false;
    while (i_ < text_.size()) {
      const char c = text_[i_];
      if (c == '\n' || c == '\r') {
        ++i_;
        continue;
      }
      if (c == '{') {
        stack_.push_back(state_);
        state_.pending_dest = true;
        // Each group directly inside the style sheet is one style.
        if (state_.in_sheet && !state_.in_style) {
          state_.in_style = true;
          entry_ = StyleEntry{};
          entry_depth_ = stack_.size();
        }
        ++i_;
        continue;
      }
      if (c == '}') {
        // Closing the document group. A last paragraph with no \par still
        // takes the properties in force inside the group.
        if (stack_.size() == 1) {
          final_indents_ = state_.indents;
          final_align_ = state_.align;
          final_marks_ = state_.marks;
          final_style_ = state_.style;
          closed_ = true;
        }
        if (!stack_.empty()) {
          const State closing = state_;
          state_ = stack_.back();
          stack_.pop_back();
          close_group(closing);
        }
        ++i_;
        continue;
      }
      if (c == '\\') {
        control();
        continue;
      }
      literal(c);
      ++i_;
    }
    // A file that ends inside its style sheet still gets the styles read.
    if (state_.in_style)
      commit_style();
    if (saw_sheet_ && !sheet_resolved_)
      resolve_styles();
    finish_paragraph(false);
    merge(doc);
    // A file from before styles: its headings take Heading 1-6.
    if (!saw_sheet_)
      adopt_heading_styles(doc, font_name(0), 11);
    if (doc.paragraphs.empty())
      doc.paragraphs.push_back(Paragraph{});
    return true;
  }

 private:
  void literal(char c)
  {
    if (state_.in_style && c == ';') {
      if (stack_.size() == entry_depth_)
        entry_.name_done = true;
      return;
    }
    if (state_.in_fonttbl && !state_.font_skip && c == ';') {
      commit_font();
      return;
    }
    if (!sink_open())
      return;
    uint32_t cp = static_cast<unsigned char>(c);
    if (cp >= 0x80) {
      // Not valid RTF, which is 7-bit, but be kind: well-formed UTF-8 is
      // taken as such, and any other byte as the declared code page.
      size_t at = i_;
      if (decode_utf8_strict(text_, at, cp))
        i_ = at - 1;
      else
        cp = cp1252(static_cast<unsigned char>(c));
    }
    put(cp);
  }

  void control()
  {
    ++i_;
    if (i_ >= text_.size())
      return;
    if (text_[i_] == '\'') {
      // \'hh: up to two real hex digits and nothing more. A bare \' is
      // dropped and leaves what follows alone.
      ++i_;
      unsigned int byte = 0;
      int digits = 0;
      while (digits < 2 && i_ < text_.size() &&
             std::isxdigit(static_cast<unsigned char>(text_[i_]))) {
        const char h = text_[i_];
        const unsigned value = std::isdigit(static_cast<unsigned char>(h))
                                   ? static_cast<unsigned>(h - '0')
                                   : static_cast<unsigned>(std::tolower(h) - 'a' + 10);
        byte = byte * 16 + value;
        ++digits;
        ++i_;
      }
      if (digits > 0)
        put(cp1252(static_cast<unsigned char>(byte)));
      return;
    }
    if (text_[i_] == '\\' || text_[i_] == '{' || text_[i_] == '}') {
      put(static_cast<unsigned char>(text_[i_]));
      ++i_;
      state_.pending_dest = false;
      return;
    }
    if (!std::isalpha(static_cast<unsigned char>(text_[i_]))) {
      const char symbol = text_[i_];
      ++i_;
      // After {\* the next word still names the destination.
      if (state_.pending_dest && symbol == '*') {
        state_.ignore = true;
        if (state_.in_fonttbl)
          state_.font_skip = true;
        return;
      }
      state_.pending_dest = false;
      return;
    }
    const size_t begin = i_;
    while (i_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[i_])))
      ++i_;
    const std::string word = text_.substr(begin, i_ - begin);
    bool has_param = false;
    int param = 0;
    int sign = 1;
    if (i_ < text_.size() &&
        (text_[i_] == '-' || std::isdigit(static_cast<unsigned char>(text_[i_])))) {
      has_param = true;
      if (text_[i_] == '-') {
        sign = -1;
        ++i_;
      }
      // Stop growing past any value this reader uses, so a long digit run
      // cannot overflow.
      while (i_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i_]))) {
        if (param < 100000000)
          param = param * 10 + (text_[i_] - '0');
        ++i_;
      }
      param *= sign;
    }
    if (i_ < text_.size() && text_[i_] == ' ')
      ++i_;
    apply(word, has_param, param);
  }

  void apply(const std::string& word, bool has_param, int param)
  {
    if (state_.pending_dest) {
      state_.pending_dest = false;
      if (word == "fonttbl") {
        state_.in_fonttbl = true;
        state_.ignore = true;
        return;
      }
      if (word == "stylesheet" && !state_.in_style) {
        state_.in_sheet = true;
        state_.ignore = true;
        saw_sheet_ = true;
        return;
      }
      if (state_.in_fonttbl && (word == "panose" || word == "falt")) {
        state_.font_skip = true;
        return;
      }
      if (list_destination(word))
        return;
      if (skip_destination(word)) {
        state_.ignore = true;
        return;
      }
    }
    if (state_.in_fonttbl && word == "f" && has_param) {
      commit_font();
      font_index_ = param;
      font_chars_.clear();
      return;
    }
    // \uN and \ucN work everywhere, the font table included.
    if (word == "uc" && has_param) {
      state_.uc = std::max(0, param);
      return;
    }
    if (word == "u" && has_param) {
      unicode(param);
      skip_fallback(state_.uc);
      return;
    }
    // A style's own words. Those in groups inside it, \*\keycode and the
    // like, are not.
    if (state_.in_style) {
      if (stack_.size() == entry_depth_)
        style_word(word, has_param, param);
      return;
    }
    if (state_.in_sheet)
      return;
    if (state_.in_pn) {
      pn_word(word, has_param, param);
      return;
    }
    if (state_.table != ListTable::None) {
      table_word(word, has_param, param);
      return;
    }
    if (state_.ignore || state_.in_fonttbl)
      return;
    if (word == "par" || word == "line") {
      // A paragraph with text took its level from its runs. An empty one
      // takes the level in force at its own \par, not the one left over
      // from the paragraph before it.
      if (paragraph_.runs.empty())
        paragraph_.heading = state_.heading;
      finish_paragraph(true);
      return;
    }
    if (word == "tab") {
      add_text("\t");
      return;
    }
    if (word == "pard") {
      // Back to Normal: its paragraph format, and its character format for
      // whatever still matched the old style.
      const ReadStyle& old = read_style(state_.style);
      const ReadStyle& normal = read_style(0);
      restyle_characters(old, normal);
      state_.style = 0;
      state_.heading = normal.heading;
      state_.indents = normal.indents;
      state_.align = normal.align;
      state_.marks = ListMarks{};
      return;
    }
    if (word == "s") {
      set_style(has_param ? param : 0);
      return;
    }
    // \qd, distributed, is East Asian Word's and has no Word 97 button: it
    // reads as left.
    if (word == "ql" || word == "qd") {
      state_.align = Align::Left;
      return;
    }
    if (word == "qj") {
      state_.align = Align::Justify;
      return;
    }
    if (word == "qc") {
      state_.align = Align::Center;
      return;
    }
    if (word == "qr") {
      state_.align = Align::Right;
      return;
    }
    // The paragraph's list: an override index and a level. The reader caps a
    // long digit run, so these stay small enough to compare and clamp.
    if (word == "ls" && has_param) {
      state_.marks.ls = param;
      return;
    }
    if (word == "ilvl" && has_param) {
      state_.marks.ilvl = std::max(0, std::min(kListLevels - 1, param));
      return;
    }
    // \lin and \rin are the leading and trailing indents Word 2000 and later
    // write beside \li and \ri. For left-to-right text they are the same.
    if ((word == "li" || word == "lin") && has_param) {
      state_.indents.left = param;
      return;
    }
    if ((word == "ri" || word == "rin") && has_param) {
      state_.indents.right = param;
      return;
    }
    if (word == "fi" && has_param) {
      state_.indents.first = param;
      return;
    }
    if (word == "plain") {
      char_seen_ = true;
      state_.bold = false;
      state_.italic = false;
      state_.underline = false;
      state_.font = 0;
      state_.half_points = 22;
      return;
    }
    if (word == "outlinelevel" && has_param) {
      // Level 9 is Word's body text.
      const int level = outline_heading(param);
      state_.heading = level;
      if (!paragraph_.runs.empty())
        paragraph_.heading = level;
      return;
    }
    if (word == "f" && has_param) {
      char_seen_ = true;
      state_.font = param;
      return;
    }
    if (word == "fs" && has_param) {
      // Half points, kept as they are: \fs21 is 10.5 pt. From \fs2, 1 pt,
      // Word's smallest, to Word's largest, 1638 pt (\fs3276), which the
      // size box can show.
      char_seen_ = true;
      state_.half_points = std::max(2 * kMinFontSize, std::min(2 * kMaxFontSize, param));
      return;
    }
    if (word == "b") {
      char_seen_ = true;
      state_.bold = !has_param || param != 0;
      return;
    }
    if (word == "i") {
      char_seen_ = true;
      state_.italic = !has_param || param != 0;
      return;
    }
    if (word == "ul") {
      char_seen_ = true;
      state_.underline = !has_param || param != 0;
      return;
    }
    if (word == "ulnone") {
      char_seen_ = true;
      state_.underline = false;
      return;
    }
  }

  // \uN is a UTF-16 code unit written as a signed 16-bit number, though
  // some writers use the unsigned spelling. Anything outside -32768..65535 is
  // not a code unit at all and reads as U+FFFD.
  void unicode(int param)
  {
    if (!sink_open())
      return;
    if (param < -32768 || param > 65535) {
      emit(kReplacement);
      return;
    }
    const auto unit = static_cast<uint32_t>(param < 0 ? param + 65536 : param);
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      flush_lead();
      lead_ = unit;
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      if (lead_ != 0) {
        const uint32_t cp = 0x10000 + (((lead_ - 0xD800) << 10) | (unit - 0xDC00));
        lead_ = 0;
        emit(cp);
      } else {
        emit(kReplacement);
      }
    } else {
      emit(unit);
    }
  }

  void emit(uint32_t cp)
  {
    put(cp);
  }

  // Whether decoded characters go anywhere: the font name being read, or the
  // text outside ignored destinations.
  bool sink_open() const
  {
    if (state_.in_style)
      return stack_.size() == entry_depth_ && !entry_.name_done;
    return state_.in_fonttbl ? !state_.font_skip : !state_.ignore;
  }

  // Every decoded character goes through here. In text, LF and CR are line
  // breaks (a paragraph, as \line is here; CR LF is one), tab is a tab, and
  // other control characters, NUL among them, are dropped. Font names take
  // no control characters at all.
  void put(uint32_t cp)
  {
    if (!sink_open())
      return;
    if (state_.in_style) {
      // A style's name. Cleaned when the sheet is resolved; a name far past
      // the cap stops growing here.
      if (entry_.name.size() < kMaxStyleName * 4 + 16)
        append_utf8(entry_.name, cp);
      return;
    }
    if (state_.in_fonttbl) {
      flush_lead();
      if (!is_control(cp))
        append_utf8(font_chars_, cp);
      return;
    }
    if (cp == '\n' || cp == '\r') {
      const bool second_half = cp == '\n' && after_cr_;
      if (!second_half)
        finish_paragraph(true);
      after_cr_ = cp == '\r' && !second_half;
      return;
    }
    if (cp == '\t') {
      add_text("\t");
      return;
    }
    if (is_control(cp))
      return;
    std::string utf8;
    append_utf8(utf8, cp);
    add_text(utf8);
  }

  // A high surrogate waits for its low half; if anything else comes first
  // it is a lone surrogate and reads as U+FFFD.
  void flush_lead()
  {
    if (lead_ == 0)
      return;
    lead_ = 0;
    put(kReplacement);
  }

  // Skips the \ucN fallback after \uN. Per the RTF spec a control word, a
  // control symbol or a \'hh escape counts as one character, line breaks do
  // not count, and the skip never crosses a group boundary.
  void skip_fallback(int count)
  {
    while (count > 0 && i_ < text_.size()) {
      const char c = text_[i_];
      if (c == '{' || c == '}')
        return;
      if (c == '\r' || c == '\n') {
        ++i_;
        continue;
      }
      if (c == '\\') {
        skip_control();
      } else {
        size_t at = i_;
        uint32_t cp = 0;
        i_ = decode_utf8_strict(text_, at, cp) ? at : i_ + 1;
      }
      --count;
    }
  }

  void skip_control()
  {
    ++i_;
    if (i_ >= text_.size())
      return;
    const char c = text_[i_];
    if (c == '\'') {
      ++i_;
      for (int n = 0;
           n < 2 && i_ < text_.size() && std::isxdigit(static_cast<unsigned char>(text_[i_])); ++n)
        ++i_;
      return;
    }
    if (!std::isalpha(static_cast<unsigned char>(c))) {
      ++i_;
      return;
    }
    while (i_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[i_])))
      ++i_;
    if (i_ < text_.size() && text_[i_] == '-')
      ++i_;
    while (i_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i_])))
      ++i_;
    if (i_ < text_.size() && text_[i_] == ' ')
      ++i_;
  }

  // The list destinations. True when `word` opened one.
  bool list_destination(const std::string& word)
  {
    if (word == "pn") {
      state_.in_pn = true;
      state_.ignore = true;
      state_.marks.pn = ListFormat{};
      return true;
    }
    if (word == "listtable") {
      state_.table = ListTable::Lists;
      state_.ignore = true;
      return true;
    }
    if (word == "listoverridetable") {
      state_.table = ListTable::Overrides;
      state_.ignore = true;
      return true;
    }
    if (state_.table == ListTable::Lists && word == "list") {
      state_.in_list = true;
      building_ = ListDef{};
      return true;
    }
    if (state_.in_list && word == "listlevel") {
      // Levels past the ninth are read and dropped.
      if (building_.levels < kListLevels + 1)
        ++building_.levels;
      return true;
    }
    if (state_.table == ListTable::Overrides && word == "listoverride") {
      state_.in_override = true;
      override_ = Override{};
      return true;
    }
    return false;
  }

  // Word 97's paragraph numbering, {\*\pn ...}.
  void pn_word(const std::string& word, bool has_param, int param)
  {
    ListFormat& pn = state_.marks.pn;
    if (word == "pnlvlblt") {
      pn = ListFormat{ListKind::Bullet, 0};
    } else if (word == "pnlvlbody") {
      pn = ListFormat{ListKind::Number, 0};
    } else if (word == "pnlvlcont") {
      // A continuation paragraph: no label of its own.
      pn = ListFormat{};
    } else if (word == "pnlvl" && has_param) {
      pn = clamp_list(ListFormat{ListKind::Number, param - 1});
    } else if (word == "pnstart" && has_param) {
      // Word 6/95's start-at, clamped by clamp_list().
      pn.start = param;
    }
  }

  void table_word(const std::string& word, bool has_param, int param)
  {
    if (state_.in_list) {
      if (word == "listid" && has_param) {
        building_.id = param;
        building_.has_id = true;
      } else if (word == "listsimple") {
        building_.simple = !has_param || param != 0;
      } else if (word == "levelstartat" && has_param && building_.levels >= 1 &&
                 building_.levels <= kListLevels) {
        building_.starts[static_cast<size_t>(building_.levels - 1)] = clamp_start(param);
      } else if ((word == "levelnfc" || word == "levelnfcn") && has_param &&
                 building_.levels >= 1 && building_.levels <= kListLevels) {
        // 23 is a bullet and 255 no number at all; the rest are numbers.
        building_.kinds[static_cast<size_t>(building_.levels - 1)] =
            param == 23 || param == 255 ? ListKind::Bullet : ListKind::Number;
      }
      return;
    }
    if (state_.in_override) {
      if (word == "listid" && has_param) {
        override_.list_id = param;
        override_.has_id = true;
      } else if (word == "ls" && has_param) {
        override_.ls = param;
      } else if (word == "listoverridestartat") {
        // The override starts its list's numbers again: a list of its own.
        override_.restarts = true;
      } else if (word == "lfolevel") {
        // One per level, in order.
        if (override_.lfo < kListLevels + 1)
          ++override_.lfo;
      } else if (word == "levelstartat" && has_param && override_.lfo >= 1 &&
                 override_.lfo <= kListLevels) {
        override_.starts[static_cast<size_t>(override_.lfo - 1)] = clamp_start(param);
      }
    }
  }

  // A group closed. `closing` is the state that was in force inside it.
  void close_group(const State& closing)
  {
    if (closing.in_style && !state_.in_style)
      commit_style();
    if (closing.in_sheet && !state_.in_sheet && !sheet_resolved_)
      resolve_styles();
    // A {\*\pn ...} group describes the paragraph around it.
    if (closing.in_pn && !state_.in_pn)
      state_.marks.pn = closing.marks.pn;
    if (closing.in_list && !state_.in_list && building_.has_id &&
        (lists_.size() < kMaxListDefs || lists_.count(building_.id) != 0))
      lists_[building_.id] = building_;
    if (closing.in_override && !state_.in_override && override_.has_id && override_.ls > 0 &&
        (overrides_.size() < kMaxListDefs || overrides_.count(override_.ls) != 0))
      overrides_[override_.ls] = override_;
  }

  // \ls through the override and list tables, then Word 97's \pn. An \ls
  // that names nothing is still a list item: it becomes a bullet.
  //
  // A numbered item counts in the \listid its \ls points at, as in Word: two
  // overrides of one list are one list, unless an override starts the
  // numbers again (\listoverridestartat). Word 6/95's \pn numbers are one
  // list. merge() relabels the lists 1 up with canonical_lists().
  ListFormat resolve(const ListMarks& marks)
  {
    const int level = std::max(0, std::min(kListLevels - 1, marks.ilvl));
    if (marks.ls > 0) {
      const auto over = overrides_.find(marks.ls);
      if (over != overrides_.end()) {
        const auto list = lists_.find(over->second.list_id);
        if (list != lists_.end()) {
          const ListDef& def = list->second;
          const size_t at = def.simple ? 0 : static_cast<size_t>(level);
          ListFormat format{def.kinds[at], level};
          if (format.kind == ListKind::Number) {
            const Override& o = over->second;
            format.list = o.restarts ? label_for(1, marks.ls) : label_for(0, o.list_id);
            // A restarting override's own start for the level, else the list's.
            const size_t lvl = static_cast<size_t>(level);
            format.start = o.restarts && o.starts[lvl] >= 0 ? o.starts[lvl] : def.starts[at];
          }
          return format;
        }
      }
      if (marks.pn.kind != ListKind::None)
        return pn_list(marks.pn);
      return ListFormat{ListKind::Bullet, level};
    }
    return pn_list(marks.pn);
  }

  ListFormat pn_list(const ListFormat& pn)
  {
    ListFormat format = clamp_list(pn);
    if (format.kind == ListKind::Number)
      format.list = label_for(2, 0);
    return format;
  }

  // A list label for a source of lists (0 a \listid, 1 a restarting \ls,
  // 2 Word 6/95's \pn) and its number there. Labels start at 1; 0 would mean
  // "continue the list above". The tables hold at most kMaxListDefs entries,
  // so the labels stay few.
  int label_for(int source, int number)
  {
    const auto key = std::make_pair(source, number);
    const auto found = labels_.find(key);
    if (found != labels_.end())
      return found->second;
    const int label = static_cast<int>(labels_.size()) + 1;
    labels_.emplace(key, label);
    return label;
  }

  // The character format in force.
  Run char_run() const
  {
    Run run;
    run.font = font_name(state_.font);
    run.size = size_from_half_points(state_.half_points);
    run.bold = state_.bold;
    run.italic = state_.italic;
    run.underline = state_.underline;
    return run;
  }

  // The \outlinelevel a heading reads as: 0 through 5 are Heading 1 through
  // 6, 9 (Word's body text) and anything past it is body text.
  static int outline_heading(int param)
  {
    if (param >= 9)
      return 0;
    return std::max(0, std::min(5, param)) + 1;
  }

  // One word inside a style sheet entry.
  void style_word(const std::string& word, bool has_param, int param)
  {
    StyleEntry& e = entry_;
    if (word == "s") {
      e.index = has_param ? param : 0;
    } else if (word == "cs" || word == "ds" || word == "ts" || word == "tsrowd") {
      // Character, section and table styles are not paragraph styles.
      e.paragraph = false;
    } else if (word == "sbasedon") {
      e.based_on = has_param ? param : -1;
    } else if (word == "snext") {
      e.next = has_param ? param : -1;
    } else if ((word == "li" || word == "lin") && has_param) {
      e.left = param;
    } else if ((word == "ri" || word == "rin") && has_param) {
      e.right = param;
    } else if (word == "fi" && has_param) {
      e.first = param;
    } else if (word == "ql" || word == "qd") {
      // \qd, distributed, reads as left, as in a paragraph.
      e.align = Align::Left;
    } else if (word == "qj") {
      e.align = Align::Justify;
    } else if (word == "qc") {
      e.align = Align::Center;
    } else if (word == "qr") {
      e.align = Align::Right;
    } else if (word == "outlinelevel" && has_param) {
      e.heading = outline_heading(param);
    } else if (word == "f" && has_param) {
      e.font = param;
    } else if (word == "fs" && has_param) {
      e.half_points = std::max(2, param);
    } else if (word == "b") {
      e.bold = !has_param || param != 0;
    } else if (word == "i") {
      e.italic = !has_param || param != 0;
    } else if (word == "ul") {
      e.underline = !has_param || param != 0;
    } else if (word == "ulnone") {
      e.underline = false;
    } else if (word == "plain") {
      e.bold = false;
      e.italic = false;
      e.underline = false;
      e.font = 0;
      e.half_points = 22;
    } else if (word == "pard") {
      e.left = 0;
      e.right = 0;
      e.first = 0;
      e.align = Align::Left;
    }
  }

  // An entry closed. The sheet stops growing at kMaxStyles. The first entry
  // for a number is the one \sN means; a later one with the same number is
  // kept too, under its own name, so its definition is not lost. It goes
  // under a key no \s can name (the reader caps parameters well inside).
  void commit_style()
  {
    if (!entry_.paragraph || styles_read_.size() >= kMaxStyles)
      return;
    if (styles_read_.count(entry_.index) != 0) {
      const int key = kSpareStyle - static_cast<int>(spare_styles_.size());
      spare_styles_.push_back(key);
      styles_read_[key] = entry_;
      return;
    }
    styles_read_[entry_.index] = entry_;
  }

  // Resolves each entry against the one it is based on, once the sheet is
  // read: Word writes every attribute, LibreOffice only those that differ
  // from the base, which may come later in the file. A base that is missing
  // or would close a circle is dropped. Iterative, so a long chain of bases
  // cannot exhaust the stack.
  void resolve_styles()
  {
    sheet_resolved_ = true;
    std::map<int, int> base;  // the base each entry keeps, if any
    for (const auto& item : styles_read_) {
      const int start = item.first;
      if (resolved_.count(start) != 0)
        continue;
      std::vector<int> path;
      std::set<int> on_path;
      int at = start;
      for (;;) {
        path.push_back(at);
        on_path.insert(at);
        const int up = styles_read_.at(at).based_on;
        if (up < 0 || styles_read_.count(up) == 0 || on_path.count(up) != 0)
          break;
        base[at] = up;
        if (resolved_.count(up) != 0)
          break;
        at = up;
      }
      for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto found = base.find(*it);
        const ReadStyle* from = found == base.end() ? nullptr : &resolved_.at(found->second);
        resolved_[*it] = resolve_one(styles_read_.at(*it), from);
      }
    }
    name_styles(base);
  }

  ReadStyle resolve_one(const StyleEntry& e, const ReadStyle* base) const
  {
    ReadStyle r = base ? *base : ReadStyle{};
    if (e.font)
      r.font = *e.font;
    if (e.half_points)
      r.half_points = *e.half_points;
    if (e.bold)
      r.bold = *e.bold;
    if (e.italic)
      r.italic = *e.italic;
    if (e.underline)
      r.underline = *e.underline;
    if (e.left)
      r.indents.left = *e.left;
    if (e.right)
      r.indents.right = *e.right;
    if (e.first)
      r.indents.first = *e.first;
    r.indents = clamp_indents(r.indents);
    if (e.align)
      r.align = *e.align;
    // A level from the file, else from a "heading N" name (LibreOffice
    // writes none in its sheet), else the base's.
    if (e.heading)
      r.heading = *e.heading;
    else if (e.index != 0)
      r.heading = name_heading(clean_style_name(e.name), r.heading);
    return r;
  }

  // Names, unique ignoring case, with the built-ins spelled as this program
  // spells them and \s0 always Normal; then the model's sheet, in file order
  // with Normal first and any missing built-ins after.
  void name_styles(const std::map<int, int>& base)
  {
    const std::vector<Style>& kBuiltins = default_styles();
    std::vector<int> order;
    if (styles_read_.count(0) != 0)
      order.push_back(0);
    for (const auto& item : styles_read_) {
      if (item.first != 0 && item.first > kSpareStyle)
        order.push_back(item.first);
    }
    // Second entries for a number, in file order, after the rest.
    order.insert(order.end(), spare_styles_.begin(), spare_styles_.end());
    std::vector<Style> sheet;
    if (styles_read_.count(0) == 0) {
      Style normal = builtin_styles(font_name(0), 11).front();
      sheet.push_back(normal);
    }
    for (int index : order) {
      std::string name;
      if (index == 0) {
        name = kNormalStyle;
      } else {
        const std::string raw = clean_style_name(styles_read_.at(index).name);
        name = raw.empty() ? "Style " + std::to_string(index) : raw;
        if (const Style* builtin = find_style(kBuiltins, name))
          name = builtin->name;
        if (find_style(sheet, name))
          name = unique_style_name(sheet, raw.empty() ? name : raw);
      }
      ReadStyle& r = resolved_.at(index);
      // A heading style's name gives its level when the file gives none.
      if (!styles_read_.at(index).heading && r.heading == 0)
        r.heading = builtin_heading(name);
      names_[index] = name;
      Style style;
      style.name = name;
      style.format.font = font_name(r.font);
      // Half points, as \fsN: a \fs21 style is 10.5 pt.
      style.format.size = size_from_half_points(std::min(2 * kMaxStyleSize, r.half_points));
      style.format.bold = r.bold;
      style.format.italic = r.italic;
      style.format.underline = r.underline;
      style.indents = r.indents;
      style.align = r.align;
      style.heading = r.heading;
      sheet.push_back(style);
    }
    // Bases and next styles by name, now every entry has one.
    size_t at = styles_read_.count(0) == 0 ? 1 : 0;
    for (int index : order) {
      Style& style = sheet[at++];
      const auto up = base.find(index);
      if (up != base.end() && index != 0)
        style.based_on = names_.at(up->second);
      const int next = styles_read_.at(index).next;
      if (next != index && names_.count(next) != 0)
        style.next = names_.at(next);
    }
    sheet_ = complete_sheet(sheet);
  }

  // A style by number: one the sheet defines, else Normal, else the
  // defaults this reader starts from.
  const ReadStyle& read_style(int index) const
  {
    auto found = resolved_.find(index);
    if (found == resolved_.end())
      found = resolved_.find(0);
    static const ReadStyle kDefault{};
    return found == resolved_.end() ? kDefault : found->second;
  }

  std::string style_name(int index) const
  {
    const auto found = names_.find(index);
    return found == names_.end() ? std::string(kNormalStyle) : found->second;
  }

  void restyle_characters(const ReadStyle& from, const ReadStyle& to)
  {
    if (state_.font == from.font)
      state_.font = to.font;
    if (state_.half_points == from.half_points)
      state_.half_points = to.half_points;
    if (state_.bold == from.bold)
      state_.bold = to.bold;
    if (state_.italic == from.italic)
      state_.italic = to.italic;
    if (state_.underline == from.underline)
      state_.underline = to.underline;
  }

  // \sN: the paragraph takes the style, attribute by attribute, as
  // apply_style() does, so words after it still win. A number the sheet does
  // not define is Normal.
  void set_style(int index)
  {
    if (resolved_.count(index) == 0)
      index = 0;
    const ReadStyle& from = read_style(state_.style);
    const ReadStyle& to = read_style(index);
    restyle_characters(from, to);
    if (state_.indents.left == from.indents.left)
      state_.indents.left = to.indents.left;
    if (state_.indents.right == from.indents.right)
      state_.indents.right = to.indents.right;
    if (state_.indents.first == from.indents.first)
      state_.indents.first = to.indents.first;
    if (state_.align == from.align)
      state_.align = to.align;
    if (state_.heading == from.heading) {
      state_.heading = to.heading;
      if (!paragraph_.runs.empty())
        paragraph_.heading = to.heading;
    }
    state_.style = index;
  }

  void add_text(const std::string& utf8)
  {
    flush_lead();
    after_cr_ = false;
    Run run = char_run();
    run.text = utf8;
    paragraph_.heading = state_.heading;
    add_run(paragraph_, std::move(run));
  }

  void finish_paragraph(bool from_par)
  {
    flush_lead();
    after_cr_ = false;
    if (!from_par && paragraph_.runs.empty() && !paragraphs_.empty())
      return;
    if (!from_par && paragraph_.runs.empty() && paragraphs_.empty() && !saw_par_)
      return;
    const bool live = from_par || !closed_;
    // An empty paragraph that gives a character format of its own, as Word
    // and Write-It write one (\pard\f0\fs40\par), keeps it: an empty 20 pt
    // line stays 20 pt. One that gives none (\pard\par) has none.
    if (paragraph_.runs.empty() && char_seen_ && live)
      paragraph_.mark = char_run();
    char_seen_ = false;
    paragraph_.indents = clamp_indents(live ? state_.indents : final_indents_);
    paragraph_.align = live ? state_.align : final_align_;
    paragraph_.style = style_name(live ? state_.style : final_style_);
    paragraphs_.push_back(paragraph_);
    marks_.push_back(live ? state_.marks : final_marks_);
    paragraph_ = Paragraph{};
    paragraph_.heading = state_.heading;
    if (from_par)
      saw_par_ = true;
  }

  void commit_font()
  {
    flush_lead();
    const std::string name = clean_text(trim(font_chars_), false);
    font_chars_.clear();
    if (name.empty())
      return;
    // Font numbers are labels, not indices: Word uses \f31500 and up.
    fonts_[font_index_] = name;
  }

  std::string font_name(int index) const
  {
    const auto found = fonts_.find(index);
    if (found != fonts_.end() && !found->second.empty())
      return found->second;
    return "Sans";
  }

  void merge(Document& doc)
  {
    doc.paragraphs.clear();
    doc.styles = sheet_;
    for (size_t i = 0; i < paragraphs_.size(); ++i) {
      Paragraph& paragraph = paragraphs_[i];
      Paragraph merged;
      merged.heading = paragraph.heading;
      merged.style = paragraph.style;
      merged.align = paragraph.align;
      merged.indents = paragraph.indents;
      merged.list = resolve(marks_[i]);
      // A list item with no indents of its own, as hand-written RTF often
      // has, takes the list's indents for its level so the label can hang.
      if (merged.list.kind != ListKind::None && merged.indents.left == 0 &&
          merged.indents.first == 0) {
        const int right = merged.indents.right;
        merged.indents = list_indents(merged.list.level);
        merged.indents.right = right;
      }
      for (Run& run : paragraph.runs) {
        run.text = clean_text(run.text, true);
        run.font = clean_text(run.font, false);
        add_run(merged, std::move(run));
      }
      if (merged.runs.empty() && paragraph.mark) {
        merged.mark = paragraph.mark;
        merged.mark->font = clean_text(merged.mark->font, false);
      }
      doc.paragraphs.push_back(std::move(merged));
    }
    canonical_lists(doc.paragraphs);
  }

  // One \list from the list table: the kind of each of its nine levels.
  struct ListDef {
    std::array<ListKind, kListLevels> kinds = [] {
      std::array<ListKind, kListLevels> all{};
      all.fill(ListKind::Number);  // \levelnfc0, RTF's default
      return all;
    }();
    std::array<int, kListLevels> starts = [] {
      std::array<int, kListLevels> all{};
      all.fill(1);  // \levelstartat1, RTF's default
      return all;
    }();
    int levels = 0;
    bool simple = false;
    int id = 0;
    bool has_id = false;
  };
  static int clamp_start(int start)
  {
    return std::max(0, std::min(kMaxListStart, start));
  }
  struct Override {
    int list_id = 0;
    bool has_id = false;
    int ls = 0;
    bool restarts = false;
    // Each \lfolevel's \levelstartat, -1 for none: 0 is a start.
    std::array<int, kListLevels> starts = [] {
      std::array<int, kListLevels> all{};
      all.fill(-1);
      return all;
    }();
    int lfo = 0;
  };
  // Enough for any real document; a hostile one cannot grow the maps further.
  static constexpr size_t kMaxListDefs = 4096;

  const std::string& text_;
  size_t i_ = 0;
  State state_;
  std::vector<State> stack_;
  std::map<int, std::string> fonts_;
  int font_index_ = 0;
  std::string font_chars_;
  Paragraph paragraph_;
  std::vector<Paragraph> paragraphs_;
  bool saw_par_ = false;
  // A character control word since the last paragraph ended: an empty
  // paragraph with one has a format of its own (Paragraph::mark).
  bool char_seen_ = false;
  Indents final_indents_;
  Align final_align_ = Align::Left;
  ListMarks final_marks_;
  std::vector<ListMarks> marks_;
  std::map<int, ListDef> lists_;
  std::map<int, Override> overrides_;
  std::map<std::pair<int, int>, int> labels_;
  ListDef building_;
  Override override_;
  bool closed_ = false;
  int final_style_ = 0;
  StyleEntry entry_;
  size_t entry_depth_ = 0;
  std::map<int, StyleEntry> styles_read_;
  // The keys of later entries for a number already read, in file order.
  static constexpr int kSpareStyle = -1100000000;
  std::vector<int> spare_styles_;
  std::map<int, ReadStyle> resolved_;
  std::map<int, std::string> names_;
  std::vector<Style> sheet_;
  bool saw_sheet_ = false;
  bool sheet_resolved_ = false;
  uint32_t lead_ = 0;
  // The last character was a CR line break, so an LF straight after it is
  // the same break.
  bool after_cr_ = false;
};

// RTF's \levelnfc for a list level: a bullet (23), or a number cycling
// decimal (0), lower-case letters (4), and lower-case roman (2) by level,
// matching list_label().
int level_nfc(ListKind kind, int level)
{
  if (kind == ListKind::Bullet)
    return 23;
  static const int kCycle[] = {0, 4, 2};
  return kCycle[level % 3];
}

// A label in RTF. The three bullets carry a fallback a reader without
// Unicode can show; the cp1252 bullet for the first.
std::string label_rtf(const std::string& label)
{
  if (label == "\xE2\x80\xA2")
    return "\\u8226\\'95";
  if (label == "\xE2\x97\xA6")
    return "\\u9702 o";
  if (label == "\xE2\x96\xAA")
    return "\\u9642 -";
  return escape_rtf(label);
}

// One \list for the list table: nine levels of one kind of list, each
// with Word's indents.
void write_list(std::ostringstream& out, ListKind kind, int id,
                const std::array<int, kListLevels>& starts)
{
  out << "{\\list\\listtemplateid" << id;
  for (int level = 0; level < kListLevels; ++level) {
    const int nfc = level_nfc(kind, level);
    const Indents indents = list_indents(level);
    out << "{\\listlevel\\levelnfc" << nfc << "\\levelnfcn" << nfc
        << "\\leveljc0\\leveljcn0\\levelfollow0\\levelstartat" << starts[static_cast<size_t>(level)]
        << "\\levelspace0\\levelindent0";
    if (kind == ListKind::Bullet)
      out << "{\\leveltext\\'01" << label_rtf(list_label(ListFormat{kind, level}, 0)) << ";}"
          << "{\\levelnumbers;}";
    else
      out << "{\\leveltext\\'02\\'0" << level << ".;}{\\levelnumbers\\'01;}";
    out << "\\fi" << indents.first << "\\li" << indents.left << "\\lin" << indents.left
        << "\\jclisttab\\tx" << indents.left << "}";
  }
  out << "{\\listname ;}\\listid" << id << "}";
}

}  // namespace

bool rtf_import(const std::string& text, Document& doc)
{
  Reader reader(text);
  return reader.parse(doc);
}

std::string rtf_export(const Document& doc)
{
  std::vector<std::string> fonts;
  auto index_of = [&](const std::string& name) {
    const std::string key = name.empty() ? "Sans" : name;
    for (size_t i = 0; i < fonts.size(); ++i) {
      if (fonts[i] == key)
        return static_cast<int>(i);
    }
    fonts.push_back(key);
    return static_cast<int>(fonts.size() - 1);
  };
  // An empty paragraph's own format (Paragraph::mark), when it has one. A
  // paragraph with text takes its format from its runs.
  auto mark_of = [](const Paragraph& paragraph) -> const Run* {
    for (const Run& run : paragraph.runs) {
      if (!run.text.empty())
        return nullptr;
    }
    return paragraph.mark ? &*paragraph.mark : nullptr;
  };
  for (const Paragraph& paragraph : doc.paragraphs) {
    for (const Run& run : paragraph.runs)
      index_of(run.font);
    if (const Run* mark = mark_of(paragraph))
      index_of(mark->font);
  }

  // Styles. A document that only uses Normal from the default sheet, as
  // every file before styles did, is written without a sheet, as before.
  // Otherwise every style is written whole, so readers need not resolve
  // \sbasedon; Normal is \s0 and the rest are numbered in sheet order.
  const std::vector<Style>& given = style_sheet(doc);
  bool any_style = given != default_styles();
  for (size_t i = 0; i < doc.paragraphs.size() && !any_style; ++i) {
    const std::string& name = doc.paragraphs[i].style;
    const Style* style = name == kNormalStyle ? nullptr : find_style(given, name);
    if (style != nullptr && style->name != kNormalStyle)
      any_style = true;
    // A heading in Normal needs the sheet too: read without one, it would be
    // taken for an M1 heading and given its Heading style.
    const int heading = doc.paragraphs[i].heading;
    if (heading >= 1 && heading <= 6 && (style == nullptr || style->name == kNormalStyle))
      any_style = true;
  }
  const std::vector<Style> sheet = any_style ? complete_sheet(given) : std::vector<Style>{};
  auto style_index = [&](const std::string& name) {
    const Style* style = find_style(sheet, name);
    return style == nullptr ? -1 : static_cast<int>(style - sheet.data());
  };
  if (any_style) {
    for (const Style& style : sheet)
      index_of(style.format.font);
  }
  if (fonts.empty())
    fonts.push_back("Sans");

  // Lists. \ls1 is every bullet. Each numbered list (list_ids()) is its own
  // Word list, \ls2 and up, so Word and LibreOffice count on past plain
  // paragraphs and bullets, and start again at a new list, as Write-It does.
  std::vector<ListFormat> lists;
  std::vector<int> overrides(doc.paragraphs.size(), 0);
  const std::vector<int> ids = list_ids(doc.paragraphs);
  int numbered = 0;
  bool any_list = false;
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    const ListFormat list = clamp_list(doc.paragraphs[i].list);
    lists.push_back(list);
    if (list.kind == ListKind::None)
      continue;
    any_list = true;
    if (list.kind == ListKind::Bullet) {
      overrides[i] = 1;
      continue;
    }
    numbered = std::max(numbered, ids[i]);
    overrides[i] = ids[i] + 1;
  }
  // Each list level's start, its first item's, as list_numbers() counts.
  std::array<int, kListLevels> ones{};
  ones.fill(1);
  std::vector<std::array<int, kListLevels>> starts(static_cast<size_t>(numbered) + 1, ones);
  std::vector<std::array<bool, kListLevels>> started(static_cast<size_t>(numbered) + 1,
                                                     std::array<bool, kListLevels>{});
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    if (lists[i].kind != ListKind::Number)
      continue;
    const size_t id = static_cast<size_t>(ids[i]);
    const size_t level = static_cast<size_t>(lists[i].level);
    if (!started[id][level]) {
      started[id][level] = true;
      starts[id][level] = lists[i].start;
    }
  }
  const std::vector<int> numbers = list_numbers(doc.paragraphs);

  std::ostringstream out;
  out << "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl";
  for (size_t i = 0; i < fonts.size(); ++i) {
    out << "{\\f" << i << "\\" << font_family(fonts[i]) << " " << escape_rtf(fonts[i], true)
        << ";}";
  }
  out << "}\n";
  if (any_list) {
    out << "{\\*\\listtable";
    write_list(out, ListKind::Bullet, 1, ones);
    for (int n = 0; n < numbered; ++n)
      write_list(out, ListKind::Number, n + 2, starts[static_cast<size_t>(n) + 1]);
    out << "}\n{\\*\\listoverridetable";
    for (int n = 1; n <= numbered + 1; ++n)
      out << "{\\listoverride\\listid" << n << "\\listoverridecount0\\ls" << n << "}";
    out << "}\n";
  }
  if (any_style) {
    out << "{\\stylesheet";
    for (size_t i = 0; i < sheet.size(); ++i) {
      const Style& style = sheet[i];
      out << "{";
      if (i != 0)
        out << "\\s" << i;
      const int base = style_index(style.based_on);
      if (i != 0 && base >= 0)
        out << "\\sbasedon" << base;
      const int next = style_index(style.next);
      out << "\\snext" << (next >= 0 ? next : static_cast<int>(i));
      // Whole, but a reader still inherits what is not written, so whatever
      // the base has and this style does not is written off.
      const Style* up = base >= 0 ? &sheet[static_cast<size_t>(base)] : nullptr;
      const Indents indents = clamp_indents(style.indents);
      const Indents up_indents = up ? clamp_indents(up->indents) : Indents{};
      if (indents.left != 0 || up_indents.left != 0)
        out << "\\li" << indents.left;
      if (indents.right != 0 || up_indents.right != 0)
        out << "\\ri" << indents.right;
      if (indents.first != 0 || up_indents.first != 0)
        out << "\\fi" << indents.first;
      if (style.align == Align::Center)
        out << "\\qc";
      else if (style.align == Align::Right)
        out << "\\qr";
      else if (style.align == Align::Justify)
        out << "\\qj";
      else if (up && up->align != Align::Left)
        out << "\\ql";
      if (style.heading >= 1 && style.heading <= 6)
        out << "\\outlinelevel" << (style.heading - 1);
      else if ((up && up->heading != 0) || builtin_heading(style.name) != 0)
        out << "\\outlinelevel9";  // a reader would take one from the base or name
      out << "\\f" << index_of(style.format.font) << "\\fs" << half_points_of(style.format.size);
      if (style.format.bold)
        out << "\\b";
      else if (up && up->format.bold)
        out << "\\b0";
      if (style.format.italic)
        out << "\\i";
      else if (up && up->format.italic)
        out << "\\i0";
      if (style.format.underline)
        out << "\\ul";
      else if (up && up->format.underline)
        out << "\\ulnone";
      // ';' ends the name, so one inside it is written as a code point.
      std::string name;
      for (char c : escape_rtf(style.name, true)) {
        if (c == ';')
          name += "\\u59?";
        else
          name.push_back(c);
      }
      out << " " << name << ";}";
    }
    out << "}\n";
  }
  bool wrote = false;
  for (size_t index = 0; index < doc.paragraphs.size(); ++index) {
    const Paragraph& paragraph = doc.paragraphs[index];
    const ListFormat list = lists[index];
    wrote = true;
    if (list.kind != ListKind::None) {
      // The label as plain text for readers without lists. Readers with them
      // skip {\pntext ...}. It is formatted as the item's first character,
      // as the label is drawn, or for an empty item as its own format
      // (Paragraph::mark), else its style: Word 97 formats it from the
      // paragraph mark.
      Run label;
      label.size = 11;
      if (const Style* own = any_style ? find_style(sheet, paragraph.style) : nullptr)
        label = own->format;
      if (const Run* mark = mark_of(paragraph))
        label = *mark;
      for (const Run& run : paragraph.runs) {
        if (run.text.empty())
          continue;
        label = run;
        break;
      }
      out << "{\\pntext\\f" << index_of(label.font) << "\\fs" << half_points_of(label.size)
          << (label.bold ? "\\b" : "") << (label.italic ? "\\i" : "") << " "
          << label_rtf(list_label(list, numbers[index])) << "\\tab}";
    }
    out << "\\pard";
    const int style = any_style ? style_index(paragraph.style) : -1;
    if (style > 0)
      out << "\\s" << style;
    // Written whole after \\sN, as Word writes it: LibreOffice takes \\pard
    // as flush left whatever the style says. Zero and left are written too
    // where the style has otherwise.
    const Style* given_style =
        any_style ? &sheet[static_cast<size_t>(std::max(0, style))] : nullptr;
    const Indents from = given_style ? clamp_indents(given_style->indents) : Indents{};
    const Indents indents = clamp_indents(paragraph.indents);
    if (indents.left != 0 || indents.left != from.left)
      out << "\\li" << indents.left;
    if (indents.right != 0 || indents.right != from.right)
      out << "\\ri" << indents.right;
    if (indents.first != 0 || indents.first != from.first)
      out << "\\fi" << indents.first;
    if (paragraph.align == Align::Center)
      out << "\\qc";
    else if (paragraph.align == Align::Right)
      out << "\\qr";
    else if (paragraph.align == Align::Justify)
      out << "\\qj";
    // Left is RTF's default, written only where the style has otherwise.
    else if (given_style && given_style->align != Align::Left)
      out << "\\ql";
    if (list.kind != ListKind::None) {
      // No {\*\pn ...} beside it: LibreOffice lets Word 6's \pn win over
      // \ls, and loses the levels and the restarts.
      out << "\\ls" << overrides[index] << "\\ilvl" << list.level;
    }
    if (paragraph.heading >= 1 && paragraph.heading <= 6)
      out << "\\outlinelevel" << (paragraph.heading - 1);
    else if (given_style && given_style->heading != 0)
      out << "\\outlinelevel9";  // body text in a heading style
    bool first = true;
    int font = -1;
    int size = -1;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    // Only what changes from the run before, all of it for the first. True
    // when it wrote a control word, which the text after needs a delimiter
    // after: a run that looks like the one before (its direct bits aside)
    // carries straight on.
    auto format = [&](const Run& run) {
      bool word = first;
      const int fi = index_of(run.font);
      if (first || fi != font) {
        out << "\\f" << fi;
        font = fi;
        word = true;
      }
      const int half = half_points_of(run.size);
      if (first || half != size) {
        out << "\\fs" << half;
        size = half;
        word = true;
      }
      if (first || run.bold != bold) {
        out << (run.bold ? "\\b" : "\\b0");
        bold = run.bold;
        word = true;
      }
      if (first || run.italic != italic) {
        out << (run.italic ? "\\i" : "\\i0");
        italic = run.italic;
        word = true;
      }
      if (first || run.underline != underline) {
        out << (run.underline ? "\\ul" : "\\ulnone");
        underline = run.underline;
        word = true;
      }
      first = false;
      return word;
    };
    for (const Run& run : paragraph.runs) {
      if (run.text.empty())
        continue;
      if (format(run))
        out << " ";
      out << escape_rtf(run.text);
    }
    // An empty paragraph's own format goes before its \par, as Word writes
    // it, so an empty 20 pt line reopens at 20 pt.
    if (const Run* mark = mark_of(paragraph))
      format(*mark);
    out << "\\par\n";
  }
  if (!wrote)
    out << "\\pard\\par\n";
  out << "}";
  return out.str();
}

}  // namespace writeit
