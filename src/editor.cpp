/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "border_draw.hpp"
#include "filename.hpp"
#include "font_sizes.hpp"
#include "open_plan.hpp"
#include "templates.hpp"
#include "para_check.hpp"
#include "spelling.hpp"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <pango/pango.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace writeit {
namespace {

constexpr int kFindNext = Gtk::RESPONSE_APPLY;
constexpr int kFindReplace = Gtk::RESPONSE_YES;
// Sits in an empty page-break paragraph so a seam tag has a character to
// hold. capture() skips it. It is not part of the document or the file.
constexpr gunichar kPageBreakPad = 0x200B;

// The map wins when it already has bytes (a file picture, possibly scaled on
// screen). A paste leaves a pixbuf with no entry; keep a PNG of that pixbuf.
Image remember_shown(std::map<GdkPixbuf*, Image>& images, const Glib::RefPtr<Gdk::Pixbuf>& pix)
{
  Image image;
  if (!pix)
    return image;
  const auto found = images.find(pix->gobj());
  if (found != images.end())
    image = found->second;
  if (!image.data.empty())
    return image;
  const Image encoded = image_from_pixbuf(pix);
  if (encoded.data.empty()) {
    if (image.alt.empty())
      image.alt = "image";
    return image;
  }
  image.data = encoded.data;
  image.type = encoded.type;
  if (image.alt.empty())
    image.alt = encoded.alt;
  images[pix->gobj()] = image;
  return image;
}
}  // namespace

bool paragraph_is_blank_item(const Glib::ustring& text)
{
  for (auto it = text.begin(); it != text.end(); ++it) {
    const gunichar ch = *it;
    if (ch != '\n' && ch != kPageBreakPad)
      return false;
  }
  return true;
}

namespace {

// Asked about the file a save would really replace, on top of the chooser.
bool confirm_replace(Gtk::Window& parent, const std::string& path)
{
  Gtk::MessageDialog dialog(parent,
                            "A file named \u201c" + Glib::path_get_basename(path) +
                                "\u201d already exists. Do you want to replace it?",
                            false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
  dialog.set_secondary_text("The file already exists in \u201c" +
                            Glib::path_get_basename(Glib::path_get_dirname(path)) +
                            "\u201d. Replacing it will overwrite its contents.");
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Replace", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_CANCEL);
  return dialog.run() == Gtk::RESPONSE_ACCEPT;
}

// Says why the chooser's answer can't be saved; the chooser stays open.
void refuse_save(Gtk::Window& parent, const Glib::ustring& primary, const Glib::ustring& secondary)
{
  Gtk::MessageDialog dialog(parent, primary, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
  dialog.set_secondary_text(secondary);
  dialog.run();
}

// Runs a save chooser until a file is chosen or the chooser is cancelled.
// GTK's own overwrite question is off: it asks about the name as typed, but
// the file written is save_name's, so "zout" would replace zout.rtf without
// a word. The question here is about the final path. A non-local location
// (no path) or a final name that is a folder is refused, and the chooser
// comes back.
std::optional<std::string> run_save_chooser(Gtk::FileChooserDialog& dialog, const FileType& type)
{
  dialog.set_do_overwrite_confirmation(false);
  while (dialog.run() == Gtk::RESPONSE_ACCEPT) {
    const auto decision = resolve_save(
        dialog.get_filename(), type,
        [](const std::string& candidate) {
          if (Glib::file_test(candidate, Glib::FILE_TEST_IS_DIR))
            return PathKind::Folder;
          return Glib::file_test(candidate, Glib::FILE_TEST_EXISTS) ? PathKind::File
                                                                    : PathKind::Missing;
        },
        [&dialog](const std::string& candidate) { return confirm_replace(dialog, candidate); });
    switch (decision.outcome) {
      case SaveOutcome::Write:
        return decision.path;
      case SaveOutcome::NoPath:
        refuse_save(dialog, "Write-It can only save to a folder on this computer.",
                    "Choose a folder on this computer, then save again.");
        break;
      case SaveOutcome::Folder:
        refuse_save(dialog,
                    "\u201c" + Glib::path_get_basename(decision.path) + "\u201d is a folder.",
                    "You can\u2019t save over a folder. Type a different file name.");
        break;
      case SaveOutcome::Declined:
        break;
    }
  }
  return std::nullopt;
}

bool known_size(double size)
{
  return valid_size(size);
}

// The screen-only tags that move a centred list item's text past a wide
// label: see MainWindow::update_list_shifts().
const std::string kListShiftPrefix = std::string("list-shift") + '\x1f';

bool is_list_shift(const std::string& name)
{
  return name.compare(0, kListShiftPrefix.size(), kListShiftPrefix) == 0;
}

// The screen-only tags that move a list item's text past a wide label: see
// MainWindow::update_list_tabs().
const std::string kListTabPrefix = std::string("list-tab") + '\x1f';

bool is_list_tab(const std::string& name)
{
  return name.compare(0, kListTabPrefix.size(), kListTabPrefix) == 0;
}

template <class Tag>
std::string tag_name(const Glib::RefPtr<Tag>& tag)
{
  return tag->property_name().get_value();
}

std::string fmt_name(const Run& run)
{
  // The size in half points, as RTF's \fsN, so 10.5 pt keeps its tag.
  // Colour, highlight, strikethrough, and script follow the direct bits. A
  // footnote marker adds its 1-based index after those. A tag from before
  // them has seven parts, or eight with the note, and still parses.
  std::string name =
      std::string("fmt") + '\x1f' + run.font + '\x1f' + std::to_string(half_points_of(run.size)) +
      '\x1f' + (run.bold ? "1" : "0") + '\x1f' + (run.italic ? "1" : "0") + '\x1f' +
      (run.underline ? "1" : "0") + '\x1f' + std::to_string(run.direct) + '\x1f' +
      std::to_string(static_cast<int>(ink_clamp(static_cast<int>(run.color)))) + '\x1f' +
      std::to_string(static_cast<int>(ink_clamp(static_cast<int>(run.highlight)))) + '\x1f' +
      (run.strike ? "1" : "0") + '\x1f' + std::to_string(static_cast<int>(run.script));
  if (run.note != 0)
    name += '\x1f' + std::to_string(run.note);
  return name;
}

bool parse_fmt(const std::string& name, Run& run)
{
  std::vector<std::string> parts;
  std::string current;
  for (char c : name) {
    if (c == '\x1f') {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  const bool legacy = parts.size() == 7 || parts.size() == 8;
  const bool faced = parts.size() == 11 || parts.size() == 12;
  if ((!legacy && !faced) || parts[0] != "fmt")
    return false;
  try {
    run.font = parts[1];
    run.size = size_from_half_points(std::stoi(parts[2]));
    run.direct = static_cast<unsigned>(std::stoul(parts[6]));
    run.color = Ink::Auto;
    run.highlight = Ink::Auto;
    run.strike = false;
    run.script = Script::None;
    run.note = 0;
    if (faced) {
      run.color = ink_clamp(std::stoi(parts[7]));
      run.highlight = ink_clamp(std::stoi(parts[8]));
      run.strike = parts[9] == "1";
      const int script = std::stoi(parts[10]);
      if (script == static_cast<int>(Script::Super))
        run.script = Script::Super;
      else if (script == static_cast<int>(Script::Sub))
        run.script = Script::Sub;
      if (parts.size() == 12)
        run.note = std::stoi(parts[11]);
    } else if (parts.size() == 8) {
      run.note = std::stoi(parts[7]);
    }
  } catch (const std::exception&) {
    return false;
  }
  run.bold = parts[3] == "1";
  run.italic = parts[4] == "1";
  run.underline = parts[5] == "1";
  run.text.clear();
  run.image.reset();
  return true;
}

// Paragraph tags hold the indents in twips, the alignment, and the list,
// with the indents the paragraph had before it joined the list so that undo
// and redo keep them, and last the style's name (which has no controls, so
// no \x1f). Every character of a paragraph, its newline included, carries
// exactly one.
void append_field(std::string& name, int value)
{
  name += '\x1f';
  name += std::to_string(value);
}

void append_spacing(std::string& name, const ParaFormat& format)
{
  append_field(name, format.spacing.before);
  append_field(name, format.spacing.after);
  append_field(name, static_cast<int>(format.spacing.rule));
  append_field(name, format.spacing.value);
  append_field(name, format.basis);
}

// Single with no space keeps the tag this program has always written. Anything
// else adds before, after, the line rule, its value, and the basis size.
bool spacing_in_tag(const ParaFormat& format)
{
  return !(format.spacing == Spacing{});
}

// Borders and shading use their own prefix. A fixed extra block on "para"
// would land on a cell tag's size for some column counts, so old tags would
// parse as the wrong shape.
bool chrome_in_tag(const ParaFormat& format)
{
  return !borders_plain(format.borders) || !shading_plain(format.shading) ||
         format.cell.borders_set || !shading_plain(format.cell.shading);
}

void append_side(std::string& name, const BorderSide& side)
{
  const BorderSide clamped = clamp_side(side);
  append_field(name, static_cast<int>(clamped.style));
  append_field(name, clamped.width);
  append_field(name, static_cast<int>(clamped.color));
  append_field(name, clamped.space);
  append_field(name, clamped.shadow ? 1 : 0);
}

void append_shade(std::string& name, const Shading& shading)
{
  const Shading clamped = clamp_shading(shading);
  append_field(name, clamped.pattern);
  append_field(name, static_cast<int>(clamped.foreground));
  append_field(name, static_cast<int>(clamped.background));
}

// 47 ints: four paragraph sides, paragraph shading, borders_set, four cell
// sides, cell shading. Sides are top, left, bottom, right.
void append_chrome(std::string& name, const ParaFormat& format)
{
  append_side(name, format.borders.top);
  append_side(name, format.borders.left);
  append_side(name, format.borders.bottom);
  append_side(name, format.borders.right);
  append_shade(name, format.shading);
  append_field(name, format.cell.borders_set ? 1 : 0);
  append_side(name, format.cell.borders.top);
  append_side(name, format.cell.borders.left);
  append_side(name, format.cell.borders.bottom);
  append_side(name, format.cell.borders.right);
  append_shade(name, format.cell.shading);
}

void append_para_chrome(std::string& name, const Borders& borders, const Shading& shading)
{
  append_side(name, borders.top);
  append_side(name, borders.left);
  append_side(name, borders.bottom);
  append_side(name, borders.right);
  append_shade(name, shading);
}

BorderSide side_from(const std::vector<int>& values, size_t at)
{
  BorderSide side;
  side.style = clamp_border_style(values[at]);
  side.width = values[at + 1];
  side.color = ink_clamp(values[at + 2]);
  side.space = values[at + 3];
  side.shadow = values[at + 4] != 0;
  return clamp_side(side);
}

Shading shading_from(const std::vector<int>& values, size_t at)
{
  Shading shading;
  shading.pattern = values[at];
  shading.foreground = ink_clamp(values[at + 1]);
  shading.background = ink_clamp(values[at + 2]);
  return clamp_shading(shading);
}

// A tab block sits in front of the tag this program already writes, under its
// own prefix, so a paragraph with no stops keeps the bytes it has always had.
enum class Peel { No, Ok, Bad };

Peel peel_tabs(const std::string& name, const char* chrome_prefix, const char* plain_prefix,
               const char* chrome_body, const char* plain_body, std::vector<TabStop>& tabs,
               std::string& body)
{
  const std::string chrome_head = std::string(chrome_prefix) + '\x1f';
  const std::string plain_head = std::string(plain_prefix) + '\x1f';
  bool chrome = false;
  std::string rest;
  if (name.compare(0, chrome_head.size(), chrome_head) == 0) {
    chrome = true;
    rest = name.substr(chrome_head.size());
  } else if (name.compare(0, plain_head.size(), plain_head) == 0) {
    rest = name.substr(plain_head.size());
  } else {
    return Peel::No;
  }
  std::vector<int> nums;
  std::string current;
  int need = -1;
  for (size_t i = 0; i <= rest.size(); ++i) {
    const bool boundary = i == rest.size() || rest[i] == '\x1f';
    if (!boundary) {
      current.push_back(rest[i]);
      continue;
    }
    if (current.empty())
      return Peel::Bad;
    try {
      nums.push_back(std::stoi(current));
    } catch (const std::exception&) {
      return Peel::Bad;
    }
    current.clear();
    if (need < 0) {
      if (nums[0] < 1 || nums[0] > kMaxTabStops)
        return Peel::Bad;
      need = 1 + nums[0] * 3;
    }
    if (static_cast<int>(nums.size()) == need) {
      body = chrome ? chrome_body : plain_body;
      if (i < rest.size())
        body.append(rest, i, std::string::npos);
      break;
    }
    if (i == rest.size())
      return Peel::Bad;
  }
  if (need < 0 || static_cast<int>(nums.size()) != need)
    return Peel::Bad;
  tabs.clear();
  for (int n = 0; n < nums[0]; ++n) {
    TabStop stop;
    stop.position = nums[static_cast<size_t>(1 + n * 3)];
    stop.align = static_cast<TabAlign>(nums[static_cast<size_t>(2 + n * 3)]);
    stop.leader = static_cast<TabLeader>(nums[static_cast<size_t>(3 + n * 3)]);
    tabs.push_back(stop);
  }
  tabs = clamp_tabs(std::move(tabs));
  if (tabs.empty())
    return Peel::Bad;
  return Peel::Ok;
}

std::string prefix_tabs(std::string name, const char* old_chrome, size_t old_chrome_len,
                        const char* old_plain, size_t old_plain_len, const char* neu_chrome,
                        const char* neu_plain, const std::vector<TabStop>& raw)
{
  const std::vector<TabStop> tabs = clamp_tabs(raw);
  if (tabs.empty())
    return name;
  const bool chrome = name.compare(0, old_chrome_len, old_chrome) == 0;
  if (!chrome && name.compare(0, old_plain_len, old_plain) != 0)
    return name;
  std::string out = chrome ? neu_chrome : neu_plain;
  append_field(out, static_cast<int>(tabs.size()));
  for (const TabStop& stop : tabs) {
    append_field(out, stop.position);
    append_field(out, static_cast<int>(stop.align));
    append_field(out, static_cast<int>(stop.leader));
  }
  out.append(name, chrome ? old_chrome_len : old_plain_len, std::string::npos);
  return out;
}

std::string para_name(const ParaFormat& format)
{
  const Indents& indents = format.indents;
  const ListFormat& list = format.list;
  const bool chrome = chrome_in_tag(format);
  std::string name = chrome ? "parab" : "para";
  for (const int value :
       {indents.left, indents.right, indents.first, static_cast<int>(format.align),
        static_cast<int>(list.kind), list.level, list.has_own ? 1 : 0, list.own.left,
        list.own.right, list.own.first, list.list, list.start, static_cast<int>(format.direct)})
    append_field(name, value);
  // A plain paragraph keeps the fourteen-part tag it has always had.
  const bool plain = !format.page_break && format.cell.table == 0 && format.cell.widths.empty();
  if (!plain) {
    append_field(name, format.page_break ? 1 : 0);
    append_field(name, format.cell.table);
    append_field(name, format.cell.row);
    append_field(name, format.cell.column);
    append_field(name, format.cell.rows);
    append_field(name, format.cell.columns);
    append_field(name, static_cast<int>(format.cell.widths.size()));
    for (int width : format.cell.widths)
      append_field(name, width);
  }
  if (spacing_in_tag(format))
    append_spacing(name, format);
  if (chrome)
    append_chrome(name, format);
  name += '\x1f';
  name += format.style;
  return prefix_tabs(std::move(name), "parab", 5, "para", 4, "paratb", "parat", format.tabs);
}

bool parse_para(const std::string& name, ParaFormat& format)
{
  // "parat" and "paratb" carry stops and peel back to the tag underneath.
  // "parab" is one character longer than "para", so a tag from before borders
  // still matches "para" and nothing else.
  std::vector<TabStop> tabs;
  std::string body;
  const Peel peeled = peel_tabs(name, "paratb", "parat", "parab", "para", tabs, body);
  if (peeled == Peel::Bad)
    return false;
  const std::string& source = peeled == Peel::Ok ? body : name;
  bool border_tag = false;
  size_t prefix = 0;
  const std::string parab = std::string("parab") + '\x1f';
  const std::string para = std::string("para") + '\x1f';
  if (source.compare(0, parab.size(), parab) == 0) {
    border_tag = true;
    prefix = parab.size();
  } else if (source.compare(0, para.size(), para) == 0) {
    prefix = para.size();
  } else {
    return false;
  }
  std::vector<std::string> parts;
  std::string current;
  for (size_t i = prefix; i <= source.size(); ++i) {
    if (i == source.size() || source[i] == '\x1f') {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(source[i]);
    }
  }
  // The 47 border ints sit immediately before the style name. Peel them so
  // the size classes below stay the ones a tag has always had.
  std::vector<int> chrome;
  if (border_tag) {
    if (parts.size() < 48)
      return false;
    const size_t first = parts.size() - 48;
    try {
      for (size_t i = first; i < parts.size() - 1; ++i)
        chrome.push_back(std::stoi(parts[i]));
    } catch (const std::exception&) {
      return false;
    }
    if (chrome.size() != 47)
      return false;
    parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(first), parts.end() - 1);
  }
  // Thirteen numbers, then the style's name. Spacing adds five numbers before
  // the style. A page break or a table cell adds its own numbers, and spacing
  // after the column widths. A tag from before spacing still parses.
  const bool legacy = parts.size() == 14;
  const bool spaced_plain = parts.size() == 19;
  int nwidths = 0;
  bool spaced_cell = false;
  if (!legacy && !spaced_plain) {
    if (parts.size() < 21)
      return false;
    try {
      nwidths = std::stoi(parts[19]);
    } catch (const std::exception&) {
      return false;
    }
    if (nwidths < 0 || nwidths > kMaxTableColumns)
      return false;
    if (parts.size() == static_cast<size_t>(21 + nwidths))
      spaced_cell = false;
    else if (parts.size() == static_cast<size_t>(26 + nwidths))
      spaced_cell = true;
    else
      return false;
  }
  const size_t numbers = legacy         ? 13
                         : spaced_plain ? 18
                                        : static_cast<size_t>(20 + nwidths + (spaced_cell ? 5 : 0));
  std::vector<int> values;
  try {
    for (size_t i = 0; i < numbers; ++i)
      values.push_back(std::stoi(parts[i]));
  } catch (const std::exception&) {
    return false;
  }
  if (values.size() != numbers || values[12] < 0 || values[3] < 0 ||
      values[3] > static_cast<int>(Align::Justify) || values[4] < 0 ||
      values[4] > static_cast<int>(ListKind::Number))
    return false;
  format.indents.left = values[0];
  format.indents.right = values[1];
  format.indents.first = values[2];
  format.align = static_cast<Align>(values[3]);
  format.list.kind = static_cast<ListKind>(values[4]);
  format.list.level = values[5];
  format.list.has_own = values[6] != 0;
  format.list.own = Indents{values[7], values[8], values[9]};
  format.list.list = values[10];
  format.list.start = values[11];
  format.direct = static_cast<unsigned>(values[12]);
  format.page_break = false;
  format.cell = Cell{};
  format.spacing = Spacing{};
  format.basis = 22;
  if (spaced_plain) {
    if (values[15] < 0 || values[15] > static_cast<int>(LineRule::Multiple))
      return false;
    format.spacing.before = values[13];
    format.spacing.after = values[14];
    format.spacing.rule = static_cast<LineRule>(values[15]);
    format.spacing.value = values[16];
    format.basis = values[17];
    format.spacing = clamp_spacing(format.spacing);
  } else if (!legacy) {
    format.page_break = values[13] != 0;
    format.cell.table = values[14];
    format.cell.row = values[15];
    format.cell.column = values[16];
    format.cell.rows = values[17];
    format.cell.columns = values[18];
    for (int i = 0; i < nwidths; ++i)
      format.cell.widths.push_back(values[static_cast<size_t>(20 + i)]);
    if (spaced_cell) {
      const size_t at = static_cast<size_t>(20 + nwidths);
      if (values[at + 2] < 0 || values[at + 2] > static_cast<int>(LineRule::Multiple))
        return false;
      format.spacing.before = values[at];
      format.spacing.after = values[at + 1];
      format.spacing.rule = static_cast<LineRule>(values[at + 2]);
      format.spacing.value = values[at + 3];
      format.basis = values[at + 4];
      format.spacing = clamp_spacing(format.spacing);
    }
  }
  format.borders = Borders{};
  format.shading = Shading{};
  if (border_tag) {
    format.borders.top = side_from(chrome, 0);
    format.borders.left = side_from(chrome, 5);
    format.borders.bottom = side_from(chrome, 10);
    format.borders.right = side_from(chrome, 15);
    format.shading = shading_from(chrome, 20);
    format.cell.borders_set = chrome[23] != 0;
    format.cell.borders.top = side_from(chrome, 24);
    format.cell.borders.left = side_from(chrome, 29);
    format.cell.borders.bottom = side_from(chrome, 34);
    format.cell.borders.right = side_from(chrome, 39);
    format.cell.shading = shading_from(chrome, 44);
  }
  format.tabs = peeled == Peel::Ok ? tabs : std::vector<TabStop>{};
  format.style = parts.back();
  return true;
}

int heading_level_name(const std::string& name)
{
  const std::string prefix = "heading-";
  if (name.compare(0, prefix.size(), prefix) != 0)
    return 0;
  try {
    const int level = std::stoi(name.substr(prefix.size()));
    if (level >= 1 && level <= 6)
      return level;
  } catch (const std::exception&) {
  }
  return 0;
}

// A cell's own paragraph tag: indents and alignment, and spacing when the
// paragraph has any. The body's para tag carries the page margins, so a cell
// does not use it. Four numbers is a cell from before spacing.
std::string cpara_name(const Indents& indents, Align align, const Spacing& spacing, int basis,
                       int heading, const Borders& borders, const Shading& shading,
                       const std::vector<TabStop>& tabs)
{
  const bool chrome = !borders_plain(borders) || !shading_plain(shading);
  std::string name = chrome ? "cparab" : "cpara";
  append_field(name, indents.left);
  append_field(name, indents.right);
  append_field(name, indents.first);
  append_field(name, static_cast<int>(align));
  if (!(spacing == Spacing{})) {
    append_field(name, spacing.before);
    append_field(name, spacing.after);
    append_field(name, static_cast<int>(spacing.rule));
    append_field(name, spacing.value);
    append_field(name, basis);
    append_field(name, heading);
  }
  if (chrome)
    append_para_chrome(name, borders, shading);
  return prefix_tabs(std::move(name), "cparab", 6, "cpara", 5, "cparatb", "cparat", tabs);
}

bool parse_cpara(const std::string& name, Indents& indents, Align& align, Spacing& spacing,
                 int& basis, int& heading, Borders& borders, Shading& shading,
                 std::vector<TabStop>& tabs)
{
  std::string body;
  const Peel peeled = peel_tabs(name, "cparatb", "cparat", "cparab", "cpara", tabs, body);
  if (peeled == Peel::Bad)
    return false;
  if (peeled != Peel::Ok)
    tabs.clear();
  const std::string& source = peeled == Peel::Ok ? body : name;
  bool border_tag = false;
  size_t prefix = 0;
  const std::string cparab = std::string("cparab") + '\x1f';
  const std::string cpara = std::string("cpara") + '\x1f';
  if (source.compare(0, cparab.size(), cparab) == 0) {
    border_tag = true;
    prefix = cparab.size();
  } else if (source.compare(0, cpara.size(), cpara) == 0) {
    prefix = cpara.size();
  } else {
    return false;
  }
  std::vector<int> values;
  std::string current;
  for (size_t i = prefix; i <= source.size(); ++i) {
    if (i == source.size() || source[i] == '\x1f') {
      if (current.empty())
        return false;
      try {
        values.push_back(std::stoi(current));
      } catch (const std::exception&) {
        return false;
      }
      current.clear();
      if (i == source.size())
        break;
    } else {
      current.push_back(source[i]);
    }
  }
  borders = Borders{};
  shading = Shading{};
  if (border_tag) {
    if (values.size() < 23)
      return false;
    const size_t base = values.size() - 23;
    if (base != 4 && base != 10)
      return false;
    borders.top = side_from(values, base);
    borders.left = side_from(values, base + 5);
    borders.bottom = side_from(values, base + 10);
    borders.right = side_from(values, base + 15);
    shading = shading_from(values, base + 20);
    values.resize(base);
  }
  if ((values.size() != 4 && values.size() != 10) || values[3] < 0 ||
      values[3] > static_cast<int>(Align::Justify))
    return false;
  indents.left = values[0];
  indents.right = values[1];
  indents.first = values[2];
  align = static_cast<Align>(values[3]);
  spacing = Spacing{};
  basis = 22;
  heading = 0;
  if (values.size() == 10) {
    if (values[6] < 0 || values[6] > static_cast<int>(LineRule::Multiple))
      return false;
    spacing.before = values[4];
    spacing.after = values[5];
    spacing.rule = static_cast<LineRule>(values[6]);
    spacing.value = values[7];
    basis = values[8];
    heading = values[9];
    spacing = clamp_spacing(spacing);
  }
  return true;
}

void add_run(Paragraph& paragraph, Run run)
{
  const bool keep = !run.text.empty() || run.image || run.note != 0;
  if (!keep)
    return;
  const bool mergeable = !run.image && run.note == 0 && !paragraph.runs.empty() &&
                         !paragraph.runs.back().image && paragraph.runs.back().note == 0 &&
                         same_format(paragraph.runs.back(), run);
  if (mergeable)
    paragraph.runs.back().text += run.text;
  else
    paragraph.runs.push_back(std::move(run));
}

}  // namespace

void MainWindow::tell(const std::string& sentence)
{
  Gtk::MessageDialog dialog(*this, sentence, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.set_title("Write-It");
  dialog.run();
}

namespace {

// A key that edits text or the selection: Delete, Backspace, Home, End,
// and Ctrl with A, C, X, V, Z or Y. The window has accelerators for some
// of them, which act on the document.
bool editing_key(const GdkEventKey* event)
{
  const guint mods = event->state & gtk_accelerator_get_default_mod_mask();
  const guint shift_ctrl = GDK_SHIFT_MASK | GDK_CONTROL_MASK;
  if ((mods & ~shift_ctrl) != 0)
    return false;
  switch (event->keyval) {
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_BackSpace:
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
    case GDK_KEY_End:
    case GDK_KEY_KP_End:
      return true;
    default:
      break;
  }
  if ((mods & GDK_CONTROL_MASK) == 0)
    return false;
  switch (gdk_keyval_to_lower(event->keyval)) {
    case GDK_KEY_a:
    case GDK_KEY_c:
    case GDK_KEY_x:
    case GDK_KEY_v:
    case GDK_KEY_z:
    case GDK_KEY_y:
      return true;
    default:
      return false;
  }
}

// An entry, or a combo box or a part of one, such as the size box.
bool field(Gtk::Widget* widget)
{
  for (GtkWidget* w = widget ? widget->gobj() : nullptr; w; w = gtk_widget_get_parent(w)) {
    if (GTK_IS_EDITABLE(w) || GTK_IS_COMBO_BOX(w))
      return true;
    if (GTK_IS_TOOLBAR(w))
      break;
  }
  return false;
}

}  // namespace

bool MainWindow::on_key_press_event(GdkEventKey* event)
{
  // GTK's window tries its accelerators before the focused widget, so the
  // Edit menu's Delete, Select All and Undo took these keys from the size
  // box and acted on the document. A field gets them first here, as Word's
  // boxes do. One it does not use (an entry has no Ctrl+Z) does nothing,
  // and never reaches the document. Other keys, Ctrl+S or Alt+F, still go
  // to the window when the field leaves them.
  Gtk::Widget* focus = get_focus();
  if (focus && focus != &text_ && field(focus)) {
    if (gtk_window_propagate_key_event(GTK_WINDOW(gobj()), event))
      return true;
    if (editing_key(event))
      return true;
  }
  return Gtk::ApplicationWindow::on_key_press_event(event);
}

void MainWindow::on_focus_moved()
{
  Gtk::Widget* focus = get_focus();
  lend_primary(focus != nullptr && focus == size_combo_.get_entry());
}

void MainWindow::lend_primary(bool lend)
{
  if (lend == primary_lent_ || !buffer_)
    return;
  // The text view adds PRIMARY to the buffer when it realizes.
  if (lend && !text_.get_realized())
    return;
  auto primary = text_.get_clipboard("PRIMARY");
  const int insert = buffer_->get_insert()->get_iter().get_offset();
  const int bound = buffer_->get_selection_bound()->get_iter().get_offset();
  const bool guard = restoring_;
  restoring_ = true;
  if (lend) {
    // Letting go of PRIMARY collapses the selection; put it back.
    buffer_->remove_selection_clipboard(primary);
    primary_lent_ = true;
  } else {
    buffer_->add_selection_clipboard(primary);
    primary_lent_ = false;
  }
  // Selecting again makes the buffer take PRIMARY back when it has it.
  if (insert != bound)
    buffer_->select_range(buffer_->get_iter_at_offset(insert), buffer_->get_iter_at_offset(bound));
  restoring_ = guard;
}

void MainWindow::build_editor()
{
  // A delete that covers a table anchor would drop the child widget. The
  // anchor character in the undo log cannot put the grid back, so the delete
  // is taken as a document edit instead. Connected before undo, and it stops
  // the signal, so the history never records the bare character.
  buffer_->signal_erase().connect([this](const Gtk::TextIter& start, const Gtk::TextIter& end) {
    if (loading_ || restoring_ || !buffer_)
      return;
    bool anchor = false;
    for (auto iter = start; iter.compare(end) < 0; ++iter) {
      if (iter.get_child_anchor()) {
        anchor = true;
        break;
      }
    }
    if (!anchor)
      return;
    const int from = start.get_offset();
    const int to = end.get_offset();
    const Document before = capture();
    capture_skip_from_ = from;
    capture_skip_to_ = to;
    const Document after = capture();
    capture_skip_from_ = -1;
    capture_skip_to_ = -1;
    g_signal_stop_emission_by_name(buffer_->gobj(), "delete-range");
    std::weak_ptr<bool> alive = alive_;
    Glib::signal_idle().connect_once([this, alive, before, after, from] {
      if (alive.expired())
        return;
      commit_document(before, after, from);
    });
  });
  // Undo records every buffer change but the screen-only list tags, which
  // are recomputed after each change and may leave the tag table.
  undo_.attach(buffer_, [](const Glib::RefPtr<Gtk::TextTag>& tag) {
    const std::string name = tag_name(tag);
    return is_list_shift(name) || is_list_tab(name) || is_page_seam(name) || is_draft_gap(name) ||
           is_spacing_gap(name);
  });
  // Undo attaches before the erase below, and takes the picture bytes here
  // while the pixbuf is still in the buffer. Undo inserts that pixbuf again.
  undo_.set_pictures(
      [this](GdkPixbuf* pix, Image& image) {
        const auto found = image_pix_.find(pix);
        if (found == image_pix_.end())
          return false;
        image = found->second;
        image_pix_.erase(found);
        return true;
      },
      [this](const Glib::RefPtr<Gdk::Pixbuf>& pix, const Image& image) {
        if (pix)
          image_pix_[pix->gobj()] = image;
      },
      [this](const Glib::RefPtr<Gdk::Pixbuf>& pix) {
        if (pix)
          image_pix_.erase(pix->gobj());
      });
  for (int level = 1; level <= 6; ++level)
    heading_tag(level);
  raise_headings();

  buffer_->signal_begin_user_action().connect(sigc::mem_fun(*this, &MainWindow::on_user_begin));
  // What normalise_paragraphs() must look at.
  buffer_->signal_insert().connect(
      [this](const Gtk::TextIter& end, const Glib::ustring& text, int) {
        note_touched(end.get_offset() - static_cast<int>(text.length()), end.get_offset());
      },
      true);
  buffer_->signal_erase().connect(
      [this](const Gtk::TextIter& start, const Gtk::TextIter&) {
        note_touched(start.get_offset(), start.get_offset());
      },
      true);
  auto touched_tag = [this](const Glib::RefPtr<Gtk::TextTag>&, const Gtk::TextIter& start,
                            const Gtk::TextIter& end) {
    note_touched(start.get_offset(), end.get_offset());
  };
  buffer_->signal_apply_tag().connect(touched_tag, true);
  buffer_->signal_remove_tag().connect(touched_tag, true);
  buffer_->signal_end_user_action().connect(sigc::mem_fun(*this, &MainWindow::on_user_end));
  buffer_->signal_insert().connect(sigc::mem_fun(*this, &MainWindow::on_inserted));
  buffer_->signal_erase().connect(sigc::mem_fun(*this, &MainWindow::on_erase), false);
  mark_set_ = buffer_->signal_mark_set().connect(sigc::mem_fun(*this, &MainWindow::on_mark_set));
  text_.signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_text_key), false);
  text_.signal_draw().connect(sigc::mem_fun(*this, &MainWindow::on_text_draw), true);
  // The list labels' line geometry (label_geometry_): an edit forgets the
  // lines it touches, before the buffer changes, and moves the ones below.
  buffer_->signal_insert().connect(
      [this](const Gtk::TextIter& at, const Glib::ustring& text, int) {
        const int line = at.get_line();
        const int added = static_cast<int>(std::count(text.begin(), text.end(), '\n'));
        forget_label_geometry(line, line, added);
      },
      false);
  buffer_->signal_erase().connect(
      [this](const Gtk::TextIter& start, const Gtk::TextIter& end) {
        forget_label_geometry(start.get_line(), end.get_line(), start.get_line() - end.get_line());
      },
      false);
  auto geometry_on_tag = [this](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                                const Gtk::TextIter& end) {
    if (seaming_ || is_page_seam(tag_name(tag)) || is_draft_gap(tag_name(tag)))
      return;
    // A range up to the next line's start, as a paragraph's is, leaves that
    // line's first character, and so its layout, untouched.
    const int last = end.get_line() - (end.starts_line() && end.compare(start) > 0 ? 1 : 0);
    forget_label_geometry(start.get_line(), std::max(start.get_line(), last), 0);
  };
  buffer_->signal_apply_tag().connect(geometry_on_tag);
  buffer_->signal_remove_tag().connect(geometry_on_tag);
  buffer_->get_tag_table()->signal_tag_changed().connect(
      [this](const Glib::RefPtr<Gtk::TextTag>&, bool) { label_geometry_.clear(); });
  buffer_->get_tag_table()->signal_tag_removed().connect(
      [this](const Glib::RefPtr<Gtk::TextTag>&) { label_geometry_.clear(); });
  // A new theme or font setting shapes text afresh.
  text_.signal_style_updated().connect([this] {
    label_geometry_.clear();
    label_layout_cache_.clear();
  });
  // The status bar's page cell follows the text and the caret.
  buffer_->signal_changed().connect([this] { queue_page_status(); });
  // Centred list items follow their label's width (update_list_shifts()), and
  // wide labels push left-aligned and justified text on (update_list_tabs()).
  buffer_->signal_changed().connect([this] {
    queue_list_shifts();
    queue_list_tabs();
  });
  // Neither pass's own tags change what either pass computes. Any other tag
  // marks its range for update_list_tabs(); a paragraph format can renumber
  // the items below, a character format changes only its own label's font.
  auto lists_on_tag = [this](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                             const Gtk::TextIter& end, bool applied) {
    if (seaming_ || shifting_ || tabbing_)
      return;
    const std::string name = tag_name(tag);
    if (is_list_shift(name) || is_list_tab(name) || is_page_seam(name) || is_draft_gap(name) ||
        is_spacing_gap(name))
      return;
    ParaFormat ignored;
    // Re-applying the tag the paragraph already has does not renumber. Taking
    // one off does. A load still renumbers: the cache may not have seen this
    // paragraph yet, and the same tag object is shared by that format.
    if (parse_para(name, ignored) &&
        (loading_ || restoring_ || !applied || !paragraph_keeps_tag(tag, start, end)))
      tabs_renumber_ = true;
    note_list_tabs(start, end);
    queue_list_shifts();
    queue_list_tabs();
  };
  buffer_->signal_apply_tag().connect(
      [lists_on_tag](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                     const Gtk::TextIter& end) { lists_on_tag(tag, start, end, true); });
  buffer_->signal_apply_tag().connect(sigc::mem_fun(*this, &MainWindow::on_tag_applied));
  buffer_->signal_remove_tag().connect(
      [lists_on_tag](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                     const Gtk::TextIter& end) { lists_on_tag(tag, start, end, false); });
  // Text edits mark their range too; a new or removed paragraph renumbers.
  buffer_->signal_insert().connect(
      [this](const Gtk::TextIter& end, const Glib::ustring& text, int) {
        auto start = end;
        start.backward_chars(static_cast<int>(text.length()));
        const bool structure = text.find('\n') != Glib::ustring::npos;
        if (structure) {
          tabs_renumber_ = true;
          // The first newline of this keystroke. A page break stays on the
          // paragraph that had it; the line Enter adds does not take another.
          if (in_user_ && inserted_newline_at_ < 0)
            inserted_newline_at_ = start.get_offset();
        }
        note_page_edit(start.get_line(), structure);
        note_list_tabs(start, end);
      },
      true);
  buffer_->signal_erase().connect(
      [this](const Gtk::TextIter& start, const Gtk::TextIter& end) {
        const bool structure =
            buffer_->get_slice(start, end, true).find('\n') != Glib::ustring::npos;
        if (structure)
          tabs_renumber_ = true;
        note_page_edit(start.get_line(), structure);
      },
      false);
  buffer_->signal_erase().connect(
      [this](const Gtk::TextIter& start, const Gtk::TextIter&) { note_list_tabs(start, start); },
      true);
  buffer_->signal_mark_set().connect(
      [this](const Gtk::TextBuffer::iterator&, const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark) {
        if (mark == buffer_->get_insert())
          queue_page_status();
      });
  // list_lines() is built again when a line comes or goes or a paragraph format changes: an edit
  // within one GTK line moves no paragraph to another line.
  buffer_->signal_insert().connect(
      [this](const Gtk::TextIter& end, const Glib::ustring& text, int) {
        auto start = end;
        start.backward_chars(static_cast<int>(text.length()));
        if (start.get_line() != end.get_line())
          list_lines_valid_ = false;
      },
      true);
  buffer_->signal_erase().connect(
      [this](const Gtk::TextIter& start, const Gtk::TextIter& end) {
        if (start.get_line() != end.get_line())
          list_lines_valid_ = false;
      },
      false);
  auto changes_on_tag = [this](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                               const Gtk::TextIter& end, bool applied) {
    ParaFormat ignored;
    if (!parse_para(tag_name(tag), ignored))
      return;
    // The apply-tag signal runs before the tag is added. A paragraph that
    // already carries it is being tagged again, so the list and the line box
    // stay as they are. Removing it does change them. A load always
    // invalidates.
    if (applied && !loading_ && !restoring_ && paragraph_keeps_tag(tag, start, end))
      return;
    list_lines_valid_ = false;
    note_page_edit(start.get_line(), true);
  };
  buffer_->signal_apply_tag().connect(
      [changes_on_tag](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                       const Gtk::TextIter& end) { changes_on_tag(tag, start, end, true); });
  buffer_->signal_remove_tag().connect(
      [changes_on_tag](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                       const Gtk::TextIter& end) { changes_on_tag(tag, start, end, false); });
  text_.signal_size_allocate().connect([this](Gtk::Allocation&) { queue_page_status(); });
  // The style list and the Table menu focus the body while the caret is still
  // in the cell. A click or a key here is the caret actually moving.
  text_.signal_button_press_event().connect(
      [this](GdkEventButton*) {
        leave_cell_edit();
        return false;
      },
      false);
  // The clipboard outlives the window: ~MainWindow disconnects this. It is
  // asked again on every change, and when the window comes back to the
  // front, in case a change was missed while it was away.
  clipboard_owner_ =
      Gtk::Clipboard::get()->signal_owner_change().connect([this](GdkEventOwnerChange*) {
        auto clipboard = Gtk::Clipboard::get();
        GObject* owner = clipboard ? gtk_clipboard_get_owner(clipboard->gobj()) : nullptr;
        if (table_copy_on_ && owner != table_copy_owner_)
          table_copy_on_ = false;
        refresh_clipboard();
      });
  signal_focus_in_event().connect(
      [this](GdkEventFocus*) {
        refresh_clipboard();
        return false;
      },
      false);

  auto activate = [](Gtk::MenuItem* item, const sigc::slot<void>& slot) {
    if (item)
      item->signal_activate().connect(slot);
  };
  auto clicked = [](Gtk::ToolButton* button, const sigc::slot<void>& slot) {
    if (button)
      button->signal_clicked().connect(slot);
  };
  activate(new_item_, [this] { new_document(true); });
  clicked(new_tool_, [this] { new_document(true); });
  activate(template_item_, [this] { on_new_from_template(); });
  activate(open_item_, [this] { open_document(); });
  clicked(open_tool_, [this] { open_document(); });
  activate(save_item_, [this] { save_document(); });
  clicked(save_tool_, [this] { save_document(); });
  activate(save_as_item_, [this] { save_document_as(); });
  activate(export_item_, [this] { export_markdown(); });
  activate(print_item_, [this] { on_print(); });
  clicked(print_tool_, [this] { on_print(); });
  activate(close_item_, [this] { close_document(); });
  activate(undo_item_, [this] { undo(); });
  clicked(undo_tool_, [this] { undo(); });
  activate(redo_item_, [this] { redo(); });
  clicked(redo_tool_, [this] { redo(); });
  activate(cut_item_, [this] {
    edit_at_caret([](Gtk::TextBuffer& buffer) { buffer.cut_clipboard(Gtk::Clipboard::get()); });
  });
  activate(context_cut_, [this] { buffer_->cut_clipboard(Gtk::Clipboard::get()); });
  clicked(cut_tool_, [this] {
    edit_at_caret([](Gtk::TextBuffer& buffer) { buffer.cut_clipboard(Gtk::Clipboard::get()); });
  });
  auto copy_edit = [this] {
    if (copy_table_selection())
      return;
    table_copy_on_ = false;
    edit_at_caret([](Gtk::TextBuffer& buffer) { buffer.copy_clipboard(Gtk::Clipboard::get()); });
  };
  auto paste_edit = [this] {
    if (paste_table_selection())
      return;
    edit_at_caret([](Gtk::TextBuffer& buffer) { buffer.paste_clipboard(Gtk::Clipboard::get()); });
  };
  activate(copy_item_, copy_edit);
  activate(context_copy_, [this] {
    if (copy_table_selection())
      return;
    table_copy_on_ = false;
    if (buffer_)
      buffer_->copy_clipboard(Gtk::Clipboard::get());
  });
  clicked(copy_tool_, copy_edit);
  activate(paste_item_, paste_edit);
  // The body's context menu. A click there has left the cell, so a band paste
  // lands as paragraphs and any other paste lands in the body.
  activate(context_paste_, [this] {
    if (paste_table_selection())
      return;
    if (buffer_)
      buffer_->paste_clipboard(Gtk::Clipboard::get());
  });
  clicked(paste_tool_, paste_edit);
  activate(delete_item_, [this] {
    edit_at_caret([](Gtk::TextBuffer& buffer) { buffer.erase_selection(true, true); });
  });
  activate(select_all_item_, [this] { buffer_->select_range(buffer_->begin(), buffer_->end()); });
  activate(find_item_, [this] { present_find(false); });
  activate(replace_item_, [this] { present_find(true); });
  activate(paragraph_item_, [this] { on_paragraph(); });
  activate(tabs_item_, [this] { on_tabs(); });
  activate(borders_item_, [this] { on_borders(); });
  activate(bullets_item_, [this] { toggle_list_kind(ListKind::Bullet); });
  activate(numbering_item_, [this] { toggle_list_kind(ListKind::Number); });
  activate(spelling_item_, [this] { on_spelling(); });
  activate(options_item_, [this] { on_options(); });

  const std::string font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  fill_font_combo(font_combo_, font);
  if (known_size(settings_.default_size))
    size_combo_.set_active_text(std::to_string(settings_.default_size));
  new_document(false);
  connect_format();
  rebuild_recent();
  update_actions();
  refresh_clipboard();
}

void MainWindow::connect_format()
{
  font_combo_.signal_changed().connect(sigc::mem_fun(*this, &MainWindow::on_font_changed));
  size_combo_.signal_changed().connect(sigc::mem_fun(*this, &MainWindow::on_size_changed));
  if (Gtk::Entry* entry = size_combo_.get_entry())
    entry->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_size_entered));
  set_focus_ = signal_set_focus().connect([this](Gtk::Widget*) { on_focus_moved(); });
  style_combo_.signal_changed().connect(sigc::mem_fun(*this, &MainWindow::on_style_chosen));
  style_combo_.property_popup_shown().signal_changed().connect([this] {
    if (!style_combo_.property_popup_shown().get_value())
      restore_edit_focus();
  });
  if (style_item_)
    style_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_style_dialog));
  // The list takes the keyboard from a cell. Put it back there, as the style
  // list does; the body is the fallback when no cell was being edited.
  font_combo_.property_popup_shown().signal_changed().connect([this] {
    if (!font_combo_.property_popup_shown().get_value())
      restore_edit_focus();
  });
  size_combo_.property_popup_shown().signal_changed().connect([this] {
    if (!size_combo_.property_popup_shown().get_value()) {
      restore_edit_focus();
      return;
    }
    // Text in the box that the list does not hold (a size being typed)
    // highlights no item. GTK would highlight the first, 8.
    // It does so after the list is shown, so this waits for that.
    if (size_combo_.get_active_row_number() < 0) {
      size_popup_idle_.disconnect();
      size_popup_idle_ = Glib::signal_idle().connect([this] {
        AtkObject* popup = gtk_combo_box_get_popup_accessible(GTK_COMBO_BOX(size_combo_.gobj()));
        GtkWidget* menu = popup ? gtk_accessible_get_widget(GTK_ACCESSIBLE(popup)) : nullptr;
        if (menu && GTK_IS_MENU_SHELL(menu) && size_combo_.get_active_row_number() < 0)
          gtk_menu_shell_deselect(GTK_MENU_SHELL(menu));
        return false;
      });
    }
  });
  if (font_item_)
    font_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_font_dialog));
  if (bold_toggle_)
    bold_toggle_->signal_toggled().connect([this] { toggle_flag(TextFlag::Bold); });
  if (italic_toggle_)
    italic_toggle_->signal_toggled().connect([this] { toggle_flag(TextFlag::Italic); });
  if (underline_toggle_)
    underline_toggle_->signal_toggled().connect([this] { toggle_flag(TextFlag::Underline); });
  if (align_left_toggle_)
    align_left_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Left); });
  if (align_center_toggle_)
    align_center_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Center); });
  if (align_right_toggle_)
    align_right_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Right); });
  if (justify_toggle_)
    justify_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Justify); });
  if (align_left_item_)
    align_left_item_->signal_activate().connect([this] { apply_align(Align::Left); });
  if (align_center_item_)
    align_center_item_->signal_activate().connect([this] { apply_align(Align::Center); });
  if (align_right_item_)
    align_right_item_->signal_activate().connect([this] { apply_align(Align::Right); });
  if (justify_item_)
    justify_item_->signal_activate().connect([this] { apply_align(Align::Justify); });
  if (bullets_toggle_)
    bullets_toggle_->signal_toggled().connect([this] { toggle_list_kind(ListKind::Bullet); });
  if (numbering_toggle_)
    numbering_toggle_->signal_toggled().connect([this] { toggle_list_kind(ListKind::Number); });
  if (bold_item_)
    bold_item_->signal_activate().connect([this] { toggle_flag(TextFlag::Bold); });
  if (italic_item_)
    italic_item_->signal_activate().connect([this] { toggle_flag(TextFlag::Italic); });
  if (underline_item_)
    underline_item_->signal_activate().connect([this] { toggle_flag(TextFlag::Underline); });
}

void MainWindow::fill_font_combo(Gtk::ComboBoxText& combo, const std::string& active)
{
  std::vector<std::string> names;
  names.push_back("Sans");
  if (auto context = text_.create_pango_context()) {
    for (const auto& family : context->list_families())
      names.emplace_back(family->get_name());
  }
  if (!active.empty())
    names.push_back(active);
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  const bool guard = suppress_format_;
  suppress_format_ = true;
  combo.remove_all();
  for (const auto& name : names)
    combo.append(name);
  if (!active.empty())
    combo.set_active_text(active);
  suppress_format_ = guard;
}

namespace {

std::string template_label(const std::string& path)
{
  std::string base = Glib::path_get_basename(path);
  const std::string ext = extension_of(base);
  if (ext == ".rtf" && base.size() > ext.size())
    base.resize(base.size() - ext.size());
  return base;
}

}  // namespace

void MainWindow::on_new_from_template()
{
  const std::vector<std::string> paths = shipped_templates();
  if (paths.empty()) {
    tell("Could not find a template.");
    return;
  }
  Gtk::Dialog dialog("New from Template", *this, true);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_New", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  auto* list = Gtk::manage(new Gtk::ListBox());
  list->set_selection_mode(Gtk::SELECTION_BROWSE);
  for (const std::string& path : paths) {
    auto* label = Gtk::manage(new Gtk::Label(template_label(path)));
    label->set_halign(Gtk::ALIGN_START);
    label->set_margin_start(8);
    label->set_margin_end(8);
    label->set_margin_top(2);
    label->set_margin_bottom(2);
    label->show();
    list->append(*label);
  }
  if (auto* row = list->get_row_at_index(0))
    list->select_row(*row);
  list->signal_row_activated().connect(
      [&dialog](Gtk::ListBoxRow*) { dialog.response(Gtk::RESPONSE_ACCEPT); });
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_size_request(260, 180);
  scroll->add(*list);
  dialog.get_content_area()->pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);
  dialog.show_all_children();
  if (dialog.run() != Gtk::RESPONSE_ACCEPT)
    return;
  int index = 0;
  if (Gtk::ListBoxRow* row = list->get_selected_row())
    index = row->get_index();
  if (index < 0 || static_cast<size_t>(index) >= paths.size())
    return;
  apply_template(paths[static_cast<size_t>(index)]);
}

bool MainWindow::apply_template(const std::string& path)
{
  if (!confirm_discard_or_save())
    return false;
  std::string bytes;
  try {
    bytes = Glib::file_get_contents(path);
  } catch (const Glib::Error&) {
    tell(unreadable_message(path));
    return false;
  }
  Document doc;
  if (!rtf_import(bytes, doc)) {
    tell(unreadable_message(path));
    return false;
  }
  // A starter, not an opened file: Save asks where to write, and closing
  // asks, because this text is not saved anywhere of the user's.
  undo_.clear();
  replace_buffer(doc, 0);
  title_name_ = Glib::path_get_basename(path);
  save_path_.clear();
  source_path_.clear();
  save_point_ = false;
  saved_state_ = undo_.state_id();
  message_.set_text("");
  update_title();
  update_actions();
  sync_format_controls();
  text_.grab_focus();
  return true;
}

bool MainWindow::new_document(bool prompt)
{
  if (prompt && !confirm_discard_or_save())
    return false;
  undo_.clear();
  save_path_.clear();
  source_path_.clear();
  title_name_ = "Untitled";
  typing_ = Run{};
  typing_.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  typing_.size = known_size(settings_.default_size) ? settings_.default_size : 11;
  save_point_ = true;
  replace_buffer(blank_document(typing_.font, typing_.size), 0);
  saved_state_ = undo_.state_id();
  message_.set_text("");
  update_title();
  update_actions();
  sync_format_controls();
  text_.grab_focus();
  return true;
}

void MainWindow::close_document()
{
  new_document(true);
}

void MainWindow::open_document()
{
  Gtk::FileChooserDialog dialog(*this, "Open", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Open", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);

  auto rtf = Gtk::FileFilter::create();
  rtf->set_name("RTF");
  rtf->add_pattern("*.rtf");
  rtf->add_pattern("*.RTF");
  auto markdown = Gtk::FileFilter::create();
  markdown->set_name("Markdown");
  markdown->add_pattern("*.md");
  markdown->add_pattern("*.markdown");
  markdown->add_pattern("*.MD");
  auto plain = Gtk::FileFilter::create();
  plain->set_name("Plain text");
  plain->add_pattern("*.txt");
  plain->add_pattern("*.TXT");
  dialog.add_filter(rtf);
  dialog.add_filter(markdown);
  dialog.add_filter(plain);

  if (dialog.run() != Gtk::RESPONSE_ACCEPT)
    return;
  OpenKind fallback = OpenKind::Rtf;
  if (auto filter = dialog.get_filter()) {
    if (filter->get_name() == "Markdown")
      fallback = OpenKind::Markdown;
    else if (filter->get_name() == "Plain text")
      fallback = OpenKind::Plain;
  }
  open_path(dialog.get_filename(), fallback);
}

bool MainWindow::open_path(const std::string& path, OpenKind fallback)
{
  if (!Glib::file_test(path, Glib::FILE_TEST_EXISTS)) {
    tell(missing_message(path));
    return false;
  }
  // Already open, under this name or another (a link, a ".." path): that
  // window comes forward instead of a second copy here.
  if (open_elsewhere_ && open_elsewhere_(path))
    return true;
  std::string bytes;
  try {
    bytes = Glib::file_get_contents(path);
  } catch (const Glib::Error&) {
    tell(unreadable_message(path));
    return false;
  }
  const std::string ext = extension_of(path);
  OpenKind kind = fallback;
  if (ext == ".rtf")
    kind = OpenKind::Rtf;
  else if (ext == ".md" || ext == ".markdown")
    kind = OpenKind::Markdown;
  else if (ext == ".txt")
    kind = OpenKind::Plain;
  Document doc;
  if (kind == OpenKind::Rtf) {
    if (!rtf_import(bytes, doc)) {
      tell(unreadable_message(path));
      return false;
    }
  } else if (kind == OpenKind::Markdown) {
    doc = markdown_import(bytes, typing_.font, typing_.size);
  } else {
    doc = plain_import(bytes, typing_.font, typing_.size);
  }
  if (!confirm_discard_or_save())
    return false;
  install_loaded(doc, path, kind == OpenKind::Rtf);
  return true;
}

bool MainWindow::open_file(const std::string& path)
{
  return open_path(path, OpenKind::Rtf);
}

bool MainWindow::pristine() const
{
  // An unedited import is not pristine: it has a file behind it
  // (source_path_), so another file must not be loaded over it.
  return save_path_.empty() && source_path_.empty() && save_point_ && !dirty();
}

void MainWindow::refuse_not_local(const std::string& uri)
{
  tell(not_local_message(uri));
}

void MainWindow::install_loaded(const Document& doc, const std::string& path, bool keep_path)
{
  undo_.clear();
  replace_buffer(doc, 0);
  title_name_ = Glib::path_get_basename(path);
  // An opened file is unmodified until it is edited, as in Word 97, RTF or
  // not. A .md or .txt is not RTF, so it keeps no save path: Save goes
  // through Save As, which offers the name with .rtf. It keeps where it came
  // from instead, so a second request for that file finds this window.
  save_path_ = keep_path ? path : std::string();
  source_path_ = keep_path ? std::string() : path;
  save_point_ = true;
  saved_state_ = undo_.state_id();
  settings_.last_dir = Glib::path_get_dirname(path);
  remember_path(path);
  message_.set_text(Glib::ustring("Opened ") + title_name_);
  update_title();
  update_actions();
  sync_format_controls();
  text_.grab_focus();
}

bool MainWindow::save_document()
{
  if (save_path_.empty())
    return save_document_as();
  return write_rtf(save_path_);
}

bool MainWindow::save_document_as()
{
  Gtk::FileChooserDialog dialog(*this, "Save As", Gtk::FILE_CHOOSER_ACTION_SAVE);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Save", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);
  auto rtf = Gtk::FileFilter::create();
  rtf->set_name("RTF");
  rtf->add_pattern("*.rtf");
  dialog.add_filter(rtf);
  const std::string suggested =
      save_path_.empty() ? title_name_ : Glib::path_get_basename(save_path_);
  dialog.set_current_name(save_name(suggested, rtf_file()));
  const auto path = run_save_chooser(dialog, rtf_file());
  if (!path)
    return false;
  return write_rtf(*path);
}

bool MainWindow::write_rtf(const std::string& path)
{
  try {
    Glib::file_set_contents(path, rtf_export(capture()));
  } catch (const Glib::Error&) {
    tell("That file could not be saved.");
    return false;
  }
  save_path_ = path;
  source_path_.clear();
  title_name_ = Glib::path_get_basename(path);
  mark_saved();
  settings_.last_dir = Glib::path_get_dirname(path);
  remember_path(path);
  message_.set_text(Glib::ustring("Saved ") + title_name_);
  update_title();
  return true;
}

void MainWindow::export_markdown()
{
  Gtk::FileChooserDialog dialog(*this, "Export", Gtk::FILE_CHOOSER_ACTION_SAVE);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Export", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);
  auto markdown = Gtk::FileFilter::create();
  markdown->set_name("Markdown");
  markdown->add_pattern("*.md");
  markdown->add_pattern("*.markdown");
  dialog.add_filter(markdown);
  std::string suggested = title_name_ == "Untitled" ? "Untitled" : title_name_;
  if (!save_path_.empty())
    suggested = Glib::path_get_basename(save_path_);
  dialog.set_current_name(save_name(suggested, markdown_file()));
  const auto chosen = run_save_chooser(dialog, markdown_file());
  if (!chosen)
    return;
  const std::string path = *chosen;
  try {
    Glib::file_set_contents(path, markdown_export(capture()));
  } catch (const Glib::Error&) {
    tell("That file could not be saved.");
    return;
  }
  settings_.last_dir = Glib::path_get_dirname(path);
  remember_path(path);
  message_.set_text(Glib::ustring("Exported ") + Glib::path_get_basename(path));
}

bool MainWindow::confirm_discard_or_save()
{
  if (!dirty())
    return true;
  Gtk::MessageDialog dialog(*this, "Save changes to " + title_name_ + "?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
  dialog.set_title("Write-It");
  dialog.add_button("Save", Gtk::RESPONSE_ACCEPT);
  dialog.add_button("Don\u2019t Save", Gtk::RESPONSE_REJECT);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  const int response = dialog.run();
  if (response == Gtk::RESPONSE_ACCEPT)
    return save_document();
  if (response == Gtk::RESPONSE_REJECT)
    return true;
  return false;
}

void MainWindow::remember_path(const std::string& path)
{
  // Other windows may have added files since this one read the ini.
  reload_recent();
  settings_.recent = push_recent(settings_.recent, path, settings_.recent_count);
  settings_.save_fields(kSetRecent | kSetLastDir);
  rebuild_recent();
}

void MainWindow::reload_recent()
{
  Settings disk;
  disk.load();
  settings_.recent = disk.recent;
}

void MainWindow::rebuild_recent()
{
  for (auto* item : recent_items_) {
    recent_menu_.remove(*item);
    delete item;
  }
  recent_items_.clear();
  for (const auto& path : settings_.recent) {
    auto* item = Gtk::manage(new Gtk::MenuItem(Glib::path_get_basename(path)));
    item->set_tooltip_text(path);
    item->signal_activate().connect([this, path] { open_path(path, OpenKind::Rtf); });
    recent_menu_.append(*item);
    recent_items_.push_back(item);
  }
  recent_menu_.show_all();
  if (recent_item_)
    recent_item_->set_sensitive(!settings_.recent.empty());
}

Document MainWindow::capture() const
{
  ++captures_;
  Document doc;
  Paragraph paragraph;
  Run run;
  bool in_run = false;
  bool have_para = false;
  auto flush_run = [&]() {
    if (!in_run)
      return;
    add_run(paragraph, run);
    run = Run{};
    in_run = false;
  };
  auto flush_paragraph = [&]() {
    flush_run();
    if (!have_para) {
      // A paragraph no tag reaches: the empty last one, or one that has not
      // been normalised yet. It follows the paragraph above.
      if (pending_para_set_) {
        paragraph.indents = pending_para_.indents;
        paragraph.align = pending_para_.align;
        paragraph.list = pending_para_.list;
        paragraph.style = pending_para_.style;
        paragraph.direct = pending_para_.direct;
        paragraph.page_break = pending_para_.page_break;
        paragraph.cell = pending_para_.cell;
        paragraph.spacing = pending_para_.spacing;
        paragraph.basis = pending_para_.basis;
        paragraph.borders = pending_para_.borders;
        paragraph.shading = pending_para_.shading;
        paragraph.tabs = pending_para_.tabs;
      } else if (!doc.paragraphs.empty()) {
        paragraph.indents = doc.paragraphs.back().indents;
        paragraph.align = doc.paragraphs.back().align;
        paragraph.list = doc.paragraphs.back().list;
        paragraph.style = doc.paragraphs.back().style;
        paragraph.direct = doc.paragraphs.back().direct;
        // A new paragraph in a cell stays in that cell. A page break belongs
        // to the paragraph that has it, and is not copied onto the next one.
        // Spacing does follow, the way the indents do.
        paragraph.cell = doc.paragraphs.back().cell;
        paragraph.spacing = doc.paragraphs.back().spacing;
        paragraph.basis = doc.paragraphs.back().basis;
        paragraph.borders = doc.paragraphs.back().borders;
        paragraph.shading = doc.paragraphs.back().shading;
        paragraph.tabs = doc.paragraphs.back().tabs;
      }
    }
    // The outline level belongs to the paragraph's style. A heading tag on
    // some of its characters, such as a word pasted from a Heading, does not
    // make the paragraph a heading.
    paragraph.heading = 0;
    if (const Style* style = find_style(styles_, paragraph.style)) {
      if (style->heading >= 1 && style->heading <= 6)
        paragraph.heading = style->heading;
    }
    doc.paragraphs.push_back(paragraph);
    paragraph = Paragraph{};
    have_para = false;
  };
  bool skip_sep = false;
  // Set only when the buffer's tail is a table anchor (and its separator).
  // A trailing newline in ordinary text still needs the final flush: that
  // newline is an empty paragraph.
  bool ended_on_anchor = false;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    const gunichar ch = iter.get_char();
    const bool omitted = capture_skip_from_ >= 0 && iter.get_offset() >= capture_skip_from_ &&
                         iter.get_offset() < capture_skip_to_;
    if (skip_sep) {
      skip_sep = false;
      if (omitted || (ch == '\n' && !iter.get_child_anchor()))
        continue;
    }
    if (omitted)
      continue;
    if (auto anchor = iter.get_child_anchor()) {
      // The anchor is one character for the whole table. A tag GTK painted
      // on it is not a paragraph of its own.
      if (in_run || !paragraph.runs.empty() || have_para)
        flush_paragraph();
      append_table(anchor, doc.paragraphs);
      paragraph = Paragraph{};
      run = Run{};
      in_run = false;
      have_para = false;
      skip_sep = true;
      ended_on_anchor = true;
      continue;
    }
    ended_on_anchor = false;
    if (!have_para) {
      ParaFormat format;
      if (auto tag = para_tag_at(iter)) {
        if (parse_para(tag_name(tag), format)) {
          paragraph.indents = format.indents;
          paragraph.align = format.align;
          paragraph.list = format.list;
          paragraph.style = format.style;
          paragraph.direct = format.direct;
          paragraph.page_break = format.page_break;
          paragraph.cell = format.cell;
          paragraph.spacing = format.spacing;
          paragraph.basis = format.basis;
          paragraph.borders = format.borders;
          paragraph.shading = format.shading;
          paragraph.tabs = format.tabs;
          have_para = true;
        }
      }
    }
    // The layout pad for an empty page-break paragraph. It is not text.
    if (ch == kPageBreakPad)
      continue;
    if (auto pix = iter.get_pixbuf()) {
      flush_run();
      Run image_run;
      image_run.image = remember_shown(image_pix_, pix);
      add_run(paragraph, std::move(image_run));
      continue;
    }
    if (ch == '\n') {
      // An empty paragraph's own format rides on its newline.
      if (!in_run && paragraph.runs.empty() && has_fmt(iter))
        paragraph.mark = format_of(iter);
      flush_paragraph();
      continue;
    }
    // Tags change only where one starts or ends, so between toggles the
    // format is the previous character's. Reading it at every character
    // cost most of a keystroke in a long document.
    if (in_run && !iter.toggles_tag()) {
      run.text += Glib::ustring(1, ch).raw();
      continue;
    }
    const Run format = format_of(iter);
    if (!in_run || !same_format(run, format)) {
      flush_run();
      run = format;
      in_run = true;
    }
    run.text += Glib::ustring(1, ch).raw();
  }
  // An anchor with nothing after it is the tail of the document. Flushing
  // again would invent an empty paragraph. A pending empty paragraph, and a
  // buffer that held none, still flush.
  const bool skip_final = ended_on_anchor && !in_run && paragraph.runs.empty() && !have_para &&
                          !pending_para_set_ && !pending_mark_set_ && !doc.paragraphs.empty();
  if (!skip_final) {
    if (!in_run && paragraph.runs.empty() && pending_mark_set_)
      paragraph.mark = pending_mark_;
    flush_paragraph();
  }
  if (doc.paragraphs.empty())
    doc.paragraphs.push_back(Paragraph{});
  doc.styles = styles_;
  doc.page = page_setup_;
  doc.default_tab = default_tab_;
  doc.header = header_;
  doc.footer = footer_;
  doc.notes = notes_;
  return doc;
}

void MainWindow::replace_buffer(const Document& doc, int offset)
{
  label_geometry_.clear();
  // A loaded document measures its notes from scratch. A height held from
  // the note being typed would pin the new page to the old band.
  note_editing_ = false;
  release_note_scroll(false);
  loading_ = true;
  page_letter_ = false;
  page_edit_maybe_ = false;
  page_cache_valid_ = false;
  cell_edit_ = CellEdit{};
  cell_typing_on_ = false;
  styles_ = doc.styles;
  const PageSetup next_page = clamp_page(doc.page);
  if (!(page_setup_ == next_page))
    styled_zoom_ = -1;
  page_setup_ = next_page;
  default_tab_ = clamp_default_tab(doc.default_tab);
  header_ = doc.header;
  footer_ = doc.footer;
  notes_ = doc.notes;
  image_pix_.clear();
  buffer_->set_text("");
  pending_para_set_ = false;
  pending_para_ = ParaFormat{};
  pending_mark_set_ = false;
  pending_mark_ = Run{};
  size_t i = 0;
  while (i < doc.paragraphs.size()) {
    if (doc.paragraphs[i].cell.table != 0) {
      const int id = doc.paragraphs[i].cell.table;
      size_t j = i + 1;
      while (j < doc.paragraphs.size() && doc.paragraphs[j].cell.table == id)
        ++j;
      std::vector<Paragraph> cells(doc.paragraphs.begin() + static_cast<std::ptrdiff_t>(i),
                                   doc.paragraphs.begin() + static_cast<std::ptrdiff_t>(j));
      mount_table(cells, j < doc.paragraphs.size());
      i = j;
      continue;
    }
    const Paragraph& paragraph = doc.paragraphs[i];
    const int para_from = buffer_->end().get_offset();
    const auto para = para_tag(para_format(paragraph));
    bool any = false;
    for (const Run& run : paragraph.runs) {
      if (run.image) {
        if (auto pix = pixbuf_for(*run.image, picture_column_limit())) {
          image_pix_[pix->gobj()] = *run.image;
          auto at = buffer_->insert_pixbuf(buffer_->end(), pix);
          auto next = at;
          if (at.get_pixbuf())
            next.forward_char();
          buffer_->apply_tag(para, at, next);
          if (paragraph.heading >= 1 && paragraph.heading <= 6)
            buffer_->apply_tag(heading_tag(paragraph.heading), at, next);
          any = true;
          continue;
        }
      }
      if (run.text.empty() && run.note == 0)
        continue;
      Run shown = run;
      if (shown.text.empty() && shown.note != 0)
        shown.text = std::to_string(shown.note);
      std::vector<Glib::RefPtr<Gtk::TextTag>> tags;
      tags.push_back(format_tag(shown));
      if (paragraph.heading >= 1 && paragraph.heading <= 6)
        tags.push_back(heading_tag(paragraph.heading));
      tags.push_back(para);
      buffer_->insert_with_tags(buffer_->end(), shown.text, tags);
      any = true;
    }
    if (i + 1 < doc.paragraphs.size()) {
      // An empty paragraph's own format goes on its newline, which draws the
      // line at that size and gives it to typing there.
      if (!any && paragraph.mark)
        buffer_->insert_with_tags(buffer_->end(), "\n", {para, format_tag(*paragraph.mark)});
      else
        buffer_->insert_with_tag(buffer_->end(), "\n", para);
    } else if (!any) {
      pending_para_ = para_format(paragraph);
      pending_para_.indents = clamp_indents(paragraph.indents);
      pending_para_.list = clamp_list(paragraph.list);
      pending_para_.spacing = clamp_spacing(paragraph.spacing);
      pending_para_.borders = clamp_borders(paragraph.borders);
      pending_para_.shading = clamp_shading(paragraph.shading);
      pending_para_.tabs = clamp_tabs(paragraph.tabs);
      pending_para_.cell.borders = clamp_borders(paragraph.cell.borders);
      pending_para_.cell.shading = clamp_shading(paragraph.cell.shading);
      pending_para_set_ = true;
      if (paragraph.mark) {
        pending_mark_ = *paragraph.mark;
        pending_mark_.text.clear();
        pending_mark_set_ = true;
      }
      // Nothing else can hold the seam: a zero-length tag does not stick, and
      // applying it again on every layout never settles. The pad is one
      // character. capture() skips it, so the paragraph stays empty.
      if (paragraph.page_break) {
        Glib::ustring pad;
        pad.push_back(kPageBreakPad);
        buffer_->insert_with_tag(buffer_->end(), pad, para);
      }
    }
    apply_spacing_gap(para_from, buffer_->end().get_offset(), para_format(paragraph),
                      paragraph.heading);
    ++i;
  }
  // Newlines carry the line height. Tag them from the paragraph they end,
  // before the caret can paint every blank line with the widget font.
  tag_line_breaks(0, buffer_->get_char_count());
  const int count = buffer_->get_char_count();
  const int place = std::max(0, std::min(offset, count));
  buffer_->place_cursor(buffer_->get_iter_at_offset(place));
  // A style apply is holding the pasteboard where the caret was. Scrolling
  // to the body insert here would send a cell on a later page back to the top.
  if (!hold_scroll_)
    text_.scroll_to(buffer_->get_insert());
  // Spaced paragraph tags were added at the top of the table. Headings, the
  // draft gap, and the seams go back above them.
  raise_headings();
  loading_ = false;
  list_lines_valid_ = false;
  refresh_stories();
  apply_page_size();
  // The sheet may be another one now: a file, a new document, undo.
  fill_style_combo();
}

void MainWindow::mark_saved()
{
  saved_state_ = undo_.state_id();
  save_point_ = true;
  // Save ends a run of typing (Greg's decision): what follows is a step of
  // its own, so one Ctrl+Z comes back to the saved state.
  end_typing_run();
}

bool MainWindow::dirty() const
{
  if (!save_point_)
    return true;
  // The history decides, not the text: clean exactly at the undo state the
  // last save (or open) left, so typing "a" then Backspace is still dirty,
  // and typing "a" then Undo is clean again.
  return undo_.state_id() != saved_state_;
}

void MainWindow::update_title()
{
  std::string title = "Write-It - " + title_name_;
  if (dirty())
    title += " *";
  set_title(title);
}

void MainWindow::edit_at_caret(const std::function<void(Gtk::TextBuffer&)>& edit)
{
  if (Gtk::TextView* view = editing_cell_view()) {
    if (auto buffer = view->get_buffer())
      edit(*buffer.get());
    // The menu and the toolbar took the keyboard. Put it back in the cell.
    // Grabbing the body would scroll the pasteboard to the body insert.
    view->grab_focus();
    return;
  }
  if (buffer_)
    edit(*buffer_.get());
}

void MainWindow::update_actions()
{
  ++actions_depth_;
  actions_depth_peak_ = std::max(actions_depth_peak_, actions_depth_);
  // Belt and braces: nothing it calls should run a main loop, but if one
  // did, an event handled there must not update the actions inside this.
  if (updating_actions_) {
    --actions_depth_;
    return;
  }
  updating_actions_ = true;
  // Cut, Copy, and Delete follow the cell while one is being edited. The
  // context menu is the body's, and a click there has already left the cell.
  bool selection = buffer_ && buffer_->get_has_selection();
  const bool body_selection = selection;
  if (Gtk::TextView* cell = editing_cell_view()) {
    auto cell_buffer = cell->get_buffer();
    selection = cell_buffer && cell_buffer->get_has_selection();
  }
  const bool any_text = buffer_ && buffer_->get_char_count() > 0;
  const bool can_undo = undo_.can_undo();
  const bool can_redo = undo_.can_redo();
  // Never wait_is_text_available(): see clipboard_text_.
  const bool can_paste = clipboard_text_;
  auto sens = [](Gtk::Widget* widget, bool on) {
    if (widget)
      widget->set_sensitive(on);
  };
  sens(undo_item_, can_undo);
  sens(redo_item_, can_redo);
  sens(undo_tool_, can_undo);
  sens(redo_tool_, can_redo);
  const bool band = table_band_on();
  sens(cut_item_, selection);
  sens(copy_item_, selection || band);
  sens(delete_item_, selection);
  sens(cut_tool_, selection);
  sens(copy_tool_, selection || band);
  sens(context_cut_, body_selection);
  sens(context_copy_, body_selection || band);
  sens(paste_item_, can_paste);
  sens(paste_tool_, can_paste);
  sens(context_paste_, can_paste);
  sens(select_all_item_, any_text);
  sens(recent_item_, !settings_.recent.empty());
  sens(page_setup_item_, true);
  sens(columns_item_, true);
  sens(picture_item_, true);
  sens(insert_table_item_, true);
  sens(table_insert_item_, true);
  sens(page_break_item_, true);
  sens(footnote_item_, notes_.size() < static_cast<size_t>(kMaxNotes));
  const bool in_cell =
      buffer_ && (focused_sheet() || sheet_at_cursor() || para_at(cursor_offset()).cell.table != 0);
  sens(table_row_item_, in_cell);
  sens(table_column_item_, in_cell);
  sens(table_delete_row_item_, in_cell);
  sens(table_delete_column_item_, in_cell);
  // Opening the Table menu leaves the cell, and the body caret may still be
  // outside the table. The cell that was edited stays available.
  sens(table_width_item_, in_cell || cell_edit_.on);
  updating_actions_ = false;
  --actions_depth_;
}

void MainWindow::refresh_clipboard()
{
  auto clipboard = Gtk::Clipboard::get();
  if (!clipboard)
    return;
  // The answer comes through the main loop, perhaps after this window has
  // closed: it holds the window's alive flag weakly and touches nothing once
  // the flag is gone.
  std::weak_ptr<bool> alive = alive_;
  clipboard->request_targets([this, alive](const std::vector<Glib::ustring>& targets) {
    if (alive.expired())
      return;
    std::vector<GdkAtom> atoms;
    atoms.reserve(targets.size());
    for (const Glib::ustring& target : targets)
      atoms.push_back(gdk_atom_intern(target.c_str(), FALSE));
    clipboard_text_ =
        !atoms.empty() && gtk_targets_include_text(atoms.data(), static_cast<int>(atoms.size()));
    apply_paste();
  });
}

void MainWindow::apply_paste()
{
  for (Gtk::Widget* widget :
       {static_cast<Gtk::Widget*>(paste_item_), static_cast<Gtk::Widget*>(paste_tool_),
        static_cast<Gtk::Widget*>(context_paste_)})
    if (widget)
      widget->set_sensitive(clipboard_text_);
}

int MainWindow::cursor_offset() const
{
  if (!buffer_)
    return 0;
  return buffer_->get_insert()->get_iter().get_offset();
}

void MainWindow::undo()
{
  // A step left open (a user action never ended) must not leave Undo dead.
  if (undo_.is_open())
    close_step(false);
  if (!undo_.can_undo())
    return;
  restoring_ = true;
  const int caret = undo_.undo();
  restoring_ = false;
  after_replay(caret);
}

void MainWindow::redo()
{
  if (undo_.is_open())
    close_step(false);
  if (!undo_.can_redo())
    return;
  restoring_ = true;
  const int caret = undo_.redo();
  restoring_ = false;
  after_replay(caret);
}

void MainWindow::after_replay(int caret)
{
  // The caret where the step began (undo: the start of a selection it
  // replaced) or where the action left it (redo), nothing selected, and
  // scrolled into view.
  const int count = buffer_->get_char_count();
  // Typing after Undo or Redo starts a step of its own.
  end_typing_run();
  restoring_ = true;
  buffer_->place_cursor(buffer_->get_iter_at_offset(std::max(0, std::min(caret, count))));
  restoring_ = false;
  text_.scroll_to(buffer_->get_insert());
  apply_page_size();
  // A replay inserts the pixbuf the step stored. The zoom did not restyle,
  // so that copy is still the size it was drawn at.
  rescale_pictures();
  // The sheet may be another one now.
  fill_style_combo();
  update_title();
  update_actions();
  sync_format_controls();
  // Undoing one item can renumber the list items below it.
  text_.queue_draw();
}

MainWindow::SideState MainWindow::side_state() const
{
  return SideState{pending_para_, pending_para_set_, pending_mark_, pending_mark_set_};
}

void MainWindow::set_side_state(const SideState& state)
{
  pending_para_ = state.pending_para;
  pending_para_set_ = state.pending_para_set;
  pending_mark_ = state.pending_mark;
  pending_mark_set_ = state.pending_mark_set;
}

bool MainWindow::pending_same(const SideState& before, const SideState& after) const
{
  // The window's own state, not the buffer: different only where the empty
  // last paragraph's format is (the pending one, else the paragraph above's),
  // "set directly" bits included, as they are in a paragraph's tag.
  if (!final_paragraph_empty())
    return true;
  const int count = buffer_->get_char_count();
  // capture() of an empty document: a default paragraph.
  const ParaFormat above = count > 0 ? para_at(count - 1) : para_format(Paragraph{});
  const ParaFormat& was = before.pending_para_set ? before.pending_para : above;
  const ParaFormat& now = after.pending_para_set ? after.pending_para : above;
  return was == now && before.pending_mark_set == after.pending_mark_set &&
         (!after.pending_mark_set || same_format(before.pending_mark, after.pending_mark));
}

void MainWindow::open_step()
{
  if (undo_.is_open())
    return;
  Gtk::TextBuffer::iterator from;
  Gtk::TextBuffer::iterator to;
  const bool selection = buffer_->get_selection_bounds(from, to);
  // Undo puts the caret back at the start of what the step changes: with a
  // selection, its start, whichever end the caret is at.
  undo_.open(selection ? from.get_offset() : cursor_offset(), selection);
  side_before_ = side_state();
}

UndoHistory::Closed MainWindow::close_step(bool may_merge)
{
  const SideState after = side_state();
  const SideState& before = side_before_;
  const bool same = before.pending_para_set == after.pending_para_set &&
                    before.pending_para == after.pending_para &&
                    before.pending_mark_set == after.pending_mark_set &&
                    same_format(before.pending_mark, after.pending_mark);
  if (!same)
    undo_.record_custom([this, before] { set_side_state(before); },
                        [this, after] { set_side_state(after); }, !pending_same(before, after));
  return undo_.close(cursor_offset(), may_merge);
}

void MainWindow::on_user_begin()
{
  if (loading_ || restoring_ || dropping_pad_)
    return;
  inserted_newline_at_ = -1;
  // A letter keeps the page height. A newline or a new paragraph format
  // clears this before the height is chosen.
  page_letter_ = true;
  in_user_ = true;
  touched_ = false;
  open_step();
}

void MainWindow::on_user_end()
{
  if (!in_user_)
    return;
  in_user_ = false;
  // While the step is still open, so undo puts the pad back with the key.
  drop_page_break_pads();
  finish_pending();
  normalise_paragraphs();
  if (pending_para_set_ && !final_paragraph_empty())
    pending_para_set_ = false;
  if (pending_mark_set_ && !final_paragraph_empty())
    pending_mark_set_ = false;
  apply_next_style();
  compact_body_notes();
  // A burst of typing, of Backspace or of Delete is one undo step. A pause,
  // a format change, or a moved caret starts a new one (UndoHistory::close).
  const gint64 now = g_get_monotonic_time();
  const bool may_merge = last_typed_us_ != 0 && now - last_typed_us_ <= 1000000;
  if (close_step(may_merge) != UndoHistory::Closed::Dropped)
    last_typed_us_ = g_get_monotonic_time();
  update_title();
  update_actions();
  apply_page_size();
  sync_format_controls();
  // An edit on one line can renumber list items on others.
  text_.queue_draw();
}

void MainWindow::on_inserted(const Gtk::TextBuffer::iterator& pos, const Glib::ustring& text,
                             int /*bytes*/)
{
  if (loading_ || restoring_ || !in_user_)
    return;
  const int end = pos.get_offset();
  const int start = end - static_cast<int>(text.length());
  if (start < end)
    note_insert(start, end);
}

void MainWindow::on_mark_set(const Gtk::TextBuffer::iterator& /*location*/,
                             const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark)
{
  if (loading_ || restoring_ || in_user_ || !buffer_)
    return;
  if (mark != buffer_->get_insert() && mark != buffer_->get_selection_bound())
    return;
  sync_format_controls();
  update_actions();
}

void MainWindow::note_insert(int start, int end)
{
  if (!pending_insert_) {
    pending_insert_ = true;
    insert_start_ = buffer_->create_mark(buffer_->get_iter_at_offset(start), true);
    insert_end_ = buffer_->create_mark(buffer_->get_iter_at_offset(end), false);
    return;
  }
  if (insert_start_ && start < insert_start_->get_iter().get_offset())
    buffer_->move_mark(insert_start_, buffer_->get_iter_at_offset(start));
  if (insert_end_ && end > insert_end_->get_iter().get_offset())
    buffer_->move_mark(insert_end_, buffer_->get_iter_at_offset(end));
}

void MainWindow::finish_pending()
{
  if (!pending_insert_)
    return;
  pending_insert_ = false;
  int start = 0;
  int end = 0;
  if (insert_start_)
    start = insert_start_->get_iter().get_offset();
  if (insert_end_)
    end = insert_end_->get_iter().get_offset();
  if (insert_start_)
    buffer_->delete_mark(insert_start_);
  if (insert_end_)
    buffer_->delete_mark(insert_end_);
  insert_start_.reset();
  insert_end_.reset();
  // Paragraph format belongs to the paragraph's end, its newline, as in
  // Word. GTK gives text inserted inside a paragraph that paragraph's tag,
  // on top of the tags a paste or drop copies from its source, so each
  // paragraph is set here to one tag (set_para()):
  // - a pasted end brings the format of the paragraph it was copied from
  //   (the para tag applied over it in this action), so a paragraph copied
  //   whole keeps its format;
  // - the paragraph the first pasted end closes, which begins with the
  //   landing paragraph's text before the insertion, takes first_end_para();
  // - the text after the last pasted end joins the paragraph it lands in
  //   and keeps that paragraph's format;
  // - with no end in the insertion, it all joins the landing paragraph.
  // An end that brought no format (Enter, plain text from another program)
  // takes the landing paragraph's.
  const auto landing = para_tag(destination_para(start, end));
  std::vector<int> ends;
  for (int n = start; n < end; ++n)
    if (buffer_->get_iter_at_offset(n).get_char() == '\n')
      ends.push_back(n);
  auto copied_at = [&](int n) {
    Glib::RefPtr<Gtk::TextTag> found;
    for (const AppliedFmt& rec : applied_para_) {
      if (rec.from->get_iter().get_offset() <= n && n < rec.to->get_iter().get_offset() &&
          buffer_->get_iter_at_offset(n).has_tag(rec.tag))
        found = rec.tag;
    }
    return found ? found : landing;
  };
  const int head_from = paragraph_start(start);
  Glib::RefPtr<Gtk::TextTag> head = landing;
  if (ends.empty()) {
    set_para(start, end, landing);
    // No paragraph end was pasted, so the word joins the landing paragraph.
    // A heading tag copied with the word does not change that paragraph.
    ParaFormat landed;
    parse_para(tag_name(landing), landed);
    // sheet() is a temporary. The pointer has to outlive that copy.
    const std::vector<Style> styles = sheet();
    const Style* style = find_style(styles, landed.style);
    const int level = style && style->heading >= 1 && style->heading <= 6 ? style->heading : 0;
    set_heading(start, end, level);
  } else {
    head = first_end_para(copied_at(ends.front()), landing);
    set_para(head_from, ends.front() + 1, head);
    for (size_t e = 1; e < ends.size(); ++e)
      set_para(ends[e - 1] + 1, ends[e] + 1, copied_at(ends[e]));
    set_para(ends.back() + 1, end, landing);
  }
  // One character format per character. GTK gives text inserted inside a
  // tag's range that tag too, so pasted or dropped text inside a run comes
  // with the run's format on top of the one its source gave it. The format
  // applied over the new text in this action (the source's, copied by a
  // paste or drop of this document's text, or Replace's) is the one it
  // keeps; text that got none (typing, plain text from another program)
  // takes the format being typed. Either way every other format tag on it
  // goes first (set_fmt()).
  std::vector<int> applied(static_cast<size_t>(std::max(0, end - start)), -1);
  for (size_t r = 0; r < applied_fmt_.size(); ++r) {
    const AppliedFmt& rec = applied_fmt_[r];
    const int from = std::max(start, rec.from->get_iter().get_offset());
    const int to = std::min(end, rec.to->get_iter().get_offset());
    for (int k = from; k < to; ++k)
      if (buffer_->get_iter_at_offset(k).has_tag(rec.tag))
        applied[static_cast<size_t>(k - start)] = static_cast<int>(r);
  }
  auto applied_at = [&](int k) { return applied[static_cast<size_t>(k - start)]; };
  for (int i = start; i < end;) {
    auto iter = buffer_->get_iter_at_offset(i);
    if (iter.get_char() == '\n') {
      ++i;
      continue;
    }
    int j = i;
    while (j < end && buffer_->get_iter_at_offset(j).get_char() != '\n')
      ++j;
    int heading = heading_near(i);
    // Text typed into an empty paragraph takes its style's outline level,
    // which an empty paragraph has nowhere to hold in the buffer.
    if (heading == 0 && paragraph_start(i) == i &&
        (j >= buffer_->get_char_count() || buffer_->get_iter_at_offset(j).get_char() == '\n')) {
      const std::vector<Style> styles = sheet();
      if (const Style* style = find_style(styles, para_at(i).style))
        heading = style->heading;
    }
    for (int a = i; a < j;) {
      const int want = applied_at(a);
      int b = a + 1;
      while (b < j && applied_at(b) == want)
        ++b;
      const auto from = buffer_->get_iter_at_offset(a);
      const auto to = buffer_->get_iter_at_offset(b);
      if (want >= 0) {
        set_fmt(from, to, applied_fmt_[static_cast<size_t>(want)].tag);
      } else {
        set_fmt(from, to, format_tag(typing_));
        if (heading > 0)
          buffer_->apply_tag(heading_tag(heading), buffer_->get_iter_at_offset(a),
                             buffer_->get_iter_at_offset(b));
      }
      a = b;
    }
    i = j;
  }
  // A new empty line (Enter on an empty line or at the start of one) takes
  // the format being typed, as Word's new paragraph mark does. A newline
  // pasted with a format of its own keeps it, and only it.
  for (int n = start; n < end; ++n) {
    auto iter = buffer_->get_iter_at_offset(n);
    if (iter.get_char() != '\n')
      continue;
    const auto next = buffer_->get_iter_at_offset(n + 1);
    if (applied_at(n) >= 0) {
      set_fmt(iter, next, applied_fmt_[static_cast<size_t>(applied_at(n))].tag);
      continue;
    }
    if (has_fmt(iter))
      continue;
    if (n > 0 && buffer_->get_iter_at_offset(n - 1).get_char() != '\n')
      continue;
    set_fmt(iter, next, format_tag(typing_));
  }
  if (!ends.empty()) {
    // The landing paragraph's own text before the insertion is now in the
    // first end's paragraph: what of its format came from its old style
    // follows the new one, as when a style is applied. The two partial
    // paragraphs take their style's outline level.
    ParaFormat was;
    ParaFormat now;
    parse_para(tag_name(landing), was);
    parse_para(tag_name(head), now);
    const std::vector<Style> styles = sheet();
    const Style* from_style = find_style(styles, was.style);
    const Style* to_style = find_style(styles, now.style);
    if (from_style && to_style && from_style != to_style) {
      for (int i = head_from; i < start;) {
        Run run = format_of(buffer_->get_iter_at_offset(i));
        int j = i + 1;
        while (j < start && same_format(format_of(buffer_->get_iter_at_offset(j)), run))
          ++j;
        restyle_run(run, *from_style, *to_style);
        set_fmt(buffer_->get_iter_at_offset(i), buffer_->get_iter_at_offset(j), format_tag(run));
        i = j;
      }
    }
    set_heading(head_from, ends.front(), to_style ? to_style->heading : 0);
    const Style* tail_style = find_style(styles, was.style);
    set_heading(ends.back() + 1, paragraph_end(ends.back() + 1),
                tail_style ? tail_style->heading : 0);
  }
  clear_applied_fmt();
  tag_line_breaks(start, end);
}

Glib::RefPtr<Gtk::TextTag> MainWindow::first_end_para(
    const Glib::RefPtr<Gtk::TextTag>& copied, const Glib::RefPtr<Gtk::TextTag>& landing) const
{
  // The paragraph a paste's first end closes starts with the landing
  // paragraph's text and ends with the copied end. Word's paragraph-mark
  // model gives it the copied paragraph's format. To give it the landing
  // paragraph's instead, return `landing` (and update paste-format's
  // expectations for the first joined paragraph).
  (void)landing;
  return copied;
}

void MainWindow::set_para(int from, int to, const Glib::RefPtr<Gtk::TextTag>& tag)
{
  // One paragraph tag per character: every other one on the range goes.
  if (from >= to)
    return;
  std::vector<Glib::RefPtr<Gtk::TextTag>> stale;
  for (auto iter = buffer_->get_iter_at_offset(from); iter.get_offset() < to; ++iter) {
    ParaFormat ignored;
    for (const auto& t : iter.get_tags())
      if (t != tag && parse_para(tag_name(t), ignored) &&
          std::find(stale.begin(), stale.end(), t) == stale.end())
        stale.push_back(t);
  }
  for (const auto& t : stale)
    buffer_->remove_tag(t, buffer_->get_iter_at_offset(from), buffer_->get_iter_at_offset(to));
  buffer_->apply_tag(tag, buffer_->get_iter_at_offset(from), buffer_->get_iter_at_offset(to));
}

void MainWindow::set_heading(int from, int to, int level)
{
  // A paragraph's text carries its outline level; its newline does not.
  if (from >= to)
    return;
  for (int l = 1; l <= 6; ++l)
    buffer_->remove_tag(heading_tag(l), buffer_->get_iter_at_offset(from),
                        buffer_->get_iter_at_offset(to));
  if (level <= 0)
    return;
  for (int i = from; i < to;) {
    if (buffer_->get_iter_at_offset(i).get_char() == '\n') {
      ++i;
      continue;
    }
    int j = i;
    while (j < to && buffer_->get_iter_at_offset(j).get_char() != '\n')
      ++j;
    buffer_->apply_tag(heading_tag(level), buffer_->get_iter_at_offset(i),
                       buffer_->get_iter_at_offset(j));
    i = j;
  }
}

Glib::RefPtr<Gtk::TextTag> MainWindow::format_tag_on(const Glib::RefPtr<Gtk::TextBuffer>& buffer,
                                                     const Run& run)
{
  if (!buffer)
    return {};
  Run key = run;
  if (key.font.empty())
    key.font = "Sans";
  if (!known_size(key.size))
    key.size = 11;
  const Glib::ustring name = fmt_name(key);
  auto table = buffer->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    tag->property_family() = key.font;
    tag->property_size_points() = key.size * zoom_factor();
    tag->property_weight() = key.bold ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL;
    tag->property_style() = key.italic ? Pango::STYLE_ITALIC : Pango::STYLE_NORMAL;
    tag->property_underline() = key.underline ? Pango::UNDERLINE_SINGLE : Pango::UNDERLINE_NONE;
    if (key.strike)
      tag->property_strikethrough() = true;
    if (key.color != Ink::Auto)
      tag->property_foreground() = ink_hex(key.color);
    if (key.highlight != Ink::Auto)
      tag->property_background() = ink_hex(key.highlight);
    // A footnote marker is a raised number, the same mark the file writes
    // with \super. That rise is not also a stored superscript. A real
    // superscript or subscript uses the same scale and rise, downwards
    // for a subscript.
    const int rise = static_cast<int>(key.size * zoom_factor() * 0.45 * Pango::SCALE);
    if (key.note != 0) {
      tag->property_scale() = 0.65;
      tag->property_rise() = rise;
    } else if (key.script != Script::None) {
      tag->property_scale() = 0.65;
      tag->property_rise() = key.script == Script::Sub ? -rise : rise;
    }
    table->add(tag);
    if (buffer == buffer_)
      raise_headings();
  }
  return tag;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::format_tag(const Run& run)
{
  return format_tag_on(buffer_, run);
}

void MainWindow::fill_cell(Gtk::TextView& view, const std::vector<Paragraph>& paragraphs)
{
  auto buffer = view.get_buffer();
  buffer->set_text("");
  int limit = picture_column_limit();
  if (!paragraphs.empty()) {
    const Cell& cell = paragraphs.front().cell;
    if (cell.column >= 0 && cell.column < static_cast<int>(cell.widths.size())) {
      const int twips = cell.widths[static_cast<size_t>(cell.column)];
      if (twips > 0) {
        const int px = column_display_px(twips, zoom_factor());
        limit = picture_cell_limit(px);
      }
    }
  }
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const Paragraph& paragraph = paragraphs[i];
    const int start = buffer->end().get_offset();
    for (const Run& run : paragraph.runs) {
      if (run.image) {
        if (auto pix = pixbuf_for(*run.image, limit)) {
          image_pix_[pix->gobj()] = *run.image;
          buffer->insert_pixbuf(buffer->end(), pix);
          continue;
        }
      }
      if (run.text.empty() && run.note == 0)
        continue;
      Run shown = run;
      if (shown.text.empty() && shown.note != 0)
        shown.text = std::to_string(shown.note);
      buffer->insert_with_tag(buffer->end(), shown.text, format_tag_on(buffer, shown));
    }
    if (i + 1 < paragraphs.size())
      buffer->insert(buffer->end(), "\n");
    // Signals are connected after fill_cell, so these tags do not re-enter
    // tag_cell_insert.
    tag_cell_paragraph(buffer, start, buffer->end().get_offset(), paragraph);
  }
}

std::vector<Paragraph> MainWindow::read_cell(const Gtk::TextView& view,
                                             const std::vector<Paragraph>& forms) const
{
  auto buffer = const_cast<Gtk::TextView&>(view).get_buffer();
  if (!buffer || buffer->get_char_count() == 0) {
    if (forms.empty())
      return {Paragraph{}};
    Paragraph paragraph = forms.front();
    paragraph.runs.clear();
    return {paragraph};
  }
  struct Line {
    std::vector<Run> runs;
    std::optional<Run> mark;
    bool saw_mark = false;
  };
  std::vector<Line> lines;
  Line line;
  Run run;
  bool in_run = false;
  auto flush_run = [&]() {
    if (!in_run)
      return;
    Paragraph holder;
    holder.runs = std::move(line.runs);
    add_run(holder, run);
    line.runs = std::move(holder.runs);
    run = Run{};
    in_run = false;
  };
  auto flush_line = [&]() {
    flush_run();
    lines.push_back(std::move(line));
    line = Line{};
  };
  bool trailing_newline = false;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    if (auto pix = iter.get_pixbuf()) {
      flush_run();
      Run image_run;
      image_run.image = remember_shown(image_pix_, pix);
      Paragraph holder;
      holder.runs = std::move(line.runs);
      add_run(holder, std::move(image_run));
      line.runs = std::move(holder.runs);
      trailing_newline = false;
      continue;
    }
    const gunichar ch = iter.get_char();
    if (ch == '\n') {
      if (!in_run && line.runs.empty() && has_fmt(iter)) {
        line.mark = format_of(iter);
        line.saw_mark = true;
      }
      flush_line();
      trailing_newline = true;
      continue;
    }
    trailing_newline = false;
    if (in_run && !iter.toggles_tag()) {
      run.text += Glib::ustring(1, ch).raw();
      continue;
    }
    const Run format = format_of(iter);
    if (!in_run || !same_format(run, format)) {
      flush_run();
      run = format;
      in_run = true;
    }
    run.text += Glib::ustring(1, ch).raw();
  }
  if (trailing_newline || lines.empty() || in_run || !line.runs.empty())
    flush_line();
  std::vector<Paragraph> out;
  out.reserve(lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    Paragraph paragraph;
    if (!forms.empty())
      paragraph = i < forms.size() ? forms[i] : forms.back();
    if (i >= forms.size())
      paragraph.page_break = false;
    paragraph.runs = std::move(lines[i].runs);
    if (paragraph.runs.empty()) {
      if (lines[i].saw_mark)
        paragraph.mark = lines[i].mark;
    } else {
      paragraph.mark.reset();
    }
    out.push_back(std::move(paragraph));
  }
  if (out.empty()) {
    if (forms.empty())
      return {Paragraph{}};
    Paragraph paragraph = forms.front();
    paragraph.runs.clear();
    return {paragraph};
  }
  return out;
}

void MainWindow::restyle_buffer(const Glib::RefPtr<Gtk::TextBuffer>& buffer)
{
  if (!buffer)
    return;
  const double zoom = zoom_factor();
  buffer->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    Run run;
    if (parse_fmt(tag_name(tag), run))
      tag->property_size_points() = run.size * zoom;
    const int level = heading_level_name(tag_name(tag));
    if (level > 0) {
      tag->property_pixels_above_lines() = static_cast<int>(8 * zoom);
      tag->property_pixels_below_lines() = static_cast<int>(4 * zoom);
    }
    Indents indents;
    Align align = Align::Left;
    Spacing spacing;
    int basis = 22;
    int heading = 0;
    Borders borders;
    Shading shading;
    std::vector<TabStop> tabs;
    if (parse_cpara(tag_name(tag), indents, align, spacing, basis, heading, borders, shading,
                    tabs)) {
      std::string font = "Sans";
      style_cell_para(tag, indents, align, spacing, basis, heading, font, tabs);
      Rgb rgb;
      if (shading_color(shading, rgb))
        tag->property_paragraph_background() = rgb_hex(rgb);
      else
        tag->property_paragraph_background_set() = false;
    }
  });
}

void MainWindow::style_cell_face(Gtk::TextView& view, const std::vector<Paragraph>& forms)
{
  auto buffer = view.get_buffer();
  if (!buffer || forms.empty())
    return;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    if (iter.get_char() == '\n')
      continue;
    if (has_fmt(iter))
      return;
  }
  const Paragraph& paragraph = forms.front();
  const Run face = paragraph.mark ? *paragraph.mark : style_typing(paragraph.style);
  Pango::FontDescription desc;
  desc.set_family(face.font.empty() ? "Sans" : face.font);
  const double size = known_size(face.size) ? face.size : 11;
  desc.set_size(static_cast<int>(size * zoom_factor() * PANGO_SCALE));
  desc.set_weight(face.bold ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL);
  desc.set_style(face.italic ? Pango::STYLE_ITALIC : Pango::STYLE_NORMAL);
  view.override_font(desc);
}

void MainWindow::on_cell_begin()
{
  if (loading_ || restoring_ || cell_step_ || undo_.is_open())
    return;
  cell_before_ = capture();
  open_step();
  cell_step_ = true;
  cell_merge_at_ = -1;
  if (Gtk::TextView* view = editing_cell_view()) {
    if (view->get_buffer())
      cell_merge_at_ = view->get_buffer()->get_insert()->get_iter().get_offset();
  }
}

void MainWindow::retitle_note_in(const Glib::RefPtr<Gtk::TextBuffer>& buffer,
                                 const std::vector<int>& remap)
{
  if (!buffer || remap.empty())
    return;
  struct Change {
    int offset = 0;
    int length = 0;
    int neu = 0;
    bool keep_text = false;
    Run format;
  };
  std::vector<Change> changes;
  auto iter = buffer->begin();
  while (!iter.is_end()) {
    if (iter.get_char() == '\n' || iter.get_pixbuf() || iter.get_child_anchor()) {
      if (!iter.forward_char())
        break;
      continue;
    }
    const Run format = format_of(iter);
    if (format.note <= 0 || format.note >= static_cast<int>(remap.size())) {
      if (!iter.forward_char())
        break;
      continue;
    }
    const int old = format.note;
    const int neu = remap[static_cast<size_t>(old)];
    const int start = iter.get_offset();
    Glib::ustring text;
    while (!iter.is_end() && iter.get_char() != '\n' && !iter.get_pixbuf() &&
           !iter.get_child_anchor()) {
      if (format_of(iter).note != old)
        break;
      text.push_back(iter.get_char());
      if (!iter.forward_char())
        break;
    }
    if (neu == old)
      continue;
    const std::string old_text = std::to_string(old);
    Change change;
    change.offset = start;
    change.length = static_cast<int>(text.length());
    change.neu = neu;
    change.format = format;
    change.format.note = std::max(0, neu);
    change.keep_text = neu > 0 && !text.empty() && text.raw() != old_text;
    if (neu > 0 || text.empty() || text.raw() == old_text)
      changes.push_back(change);
  }
  for (auto change = changes.rbegin(); change != changes.rend(); ++change) {
    auto from = buffer->get_iter_at_offset(change->offset);
    auto to = buffer->get_iter_at_offset(change->offset + change->length);
    int length = change->length;
    if (change->neu <= 0) {
      buffer->erase(from, to);
      continue;
    }
    if (!change->keep_text) {
      buffer->erase(from, to);
      const std::string next = std::to_string(change->neu);
      buffer->insert(buffer->get_iter_at_offset(change->offset), next);
      length = static_cast<int>(Glib::ustring(next).length());
    }
    set_fmt_on(buffer, buffer->get_iter_at_offset(change->offset),
               buffer->get_iter_at_offset(change->offset + length),
               format_tag_on(buffer, change->format));
  }
}

void MainWindow::retitle_note_markers(const std::vector<int>& remap, bool body, bool cells)
{
  if (body)
    retitle_note_in(buffer_, remap);
  if (!cells)
    return;
  each_cell_buffer(
      [&](const Glib::RefPtr<Gtk::TextBuffer>& buffer) { retitle_note_in(buffer, remap); });
}

namespace {

std::vector<int> inverse_note_remap(const std::vector<int>& remap)
{
  int highest = 0;
  for (int neu : remap)
    highest = std::max(highest, neu);
  std::vector<int> inverse(static_cast<size_t>(highest) + 1, 0);
  for (int old = 1; old < static_cast<int>(remap.size()); ++old) {
    const int neu = remap[static_cast<size_t>(old)];
    if (neu > 0)
      inverse[static_cast<size_t>(neu)] = old;
  }
  return inverse;
}

}  // namespace

void MainWindow::note_page_edit(int line, bool structure)
{
  if (seaming_ || dropping_pad_ || loading_ || restoring_)
    return;
  if (structure) {
    page_edit_maybe_ = false;
    page_letter_ = false;
    return;
  }
  if (!page_edit_maybe_) {
    page_edit_maybe_ = true;
    page_edit_line_ = line;
    return;
  }
  if (line != page_edit_line_) {
    page_edit_maybe_ = false;
    page_letter_ = false;
  }
}

bool MainWindow::page_edit_keeps_height() const
{
  if (!page_letter_ || !page_cache_valid_)
    return false;
  if (view_ != page_cache_view_ || zoom_factor() != page_cache_zoom_)
    return false;
  if (text_.get_allocated_width() != page_cache_width_ || page_cache_width_ <= 1)
    return false;
  int width = 0;
  int height = 0;
  page_.get_size_request(width, height);
  return height > 0;
}

bool MainWindow::page_box_unchanged() const
{
  if (!page_edit_maybe_ || !page_cache_valid_ || !buffer_)
    return false;
  if (page_edit_line_ != page_cache_line_ || page_edit_line_ < 0)
    return false;
  if (page_edit_line_ >= buffer_->get_line_count())
    return false;
  if (view_ != page_cache_view_ || zoom_factor() != page_cache_zoom_)
    return false;
  if (text_.get_allocated_width() != page_cache_width_ || page_cache_width_ <= 1)
    return false;
  if (buffer_->get_line_count() != page_cache_lines_)
    return false;
  if (note_block_px() != page_cache_notes_)
    return false;
  int y = 0;
  int height = 0;
  text_.get_line_yrange(buffer_->get_iter_at_line(page_edit_line_), y, height);
  return y == page_cache_y_ && height == page_cache_h_ && height > 0;
}

void MainWindow::remember_page_cache()
{
  if (!buffer_ || text_.get_allocated_width() <= 1)
    return;
  const auto caret = buffer_->get_iter_at_mark(buffer_->get_insert());
  int y = 0;
  int height = 0;
  text_.get_line_yrange(caret, y, height);
  if (height <= 0)
    return;
  page_cache_line_ = caret.get_line();
  page_cache_y_ = y;
  page_cache_h_ = height;
  page_cache_lines_ = buffer_->get_line_count();
  page_cache_width_ = text_.get_allocated_width();
  page_cache_notes_ = note_block_px();
  page_cache_zoom_ = zoom_factor();
  page_cache_view_ = view_;
  page_cache_valid_ = true;
}

bool MainWindow::paragraph_keeps_tag(const Glib::RefPtr<Gtk::TextTag>& tag,
                                     const Gtk::TextIter& start, const Gtk::TextIter& /*end*/) const
{
  // Called from apply-tag, which runs before the tag is added. The paragraph
  // already has it when its first character does, including a re-apply that
  // starts there. An apply that starts later keeps the list cache when some
  // earlier character already has it: that is the character the cache read.
  if (!buffer_ || !tag)
    return false;
  const int from = paragraph_start(start.get_offset());
  if (buffer_->get_iter_at_offset(from).has_tag(tag))
    return true;
  if (start.get_offset() <= from)
    return false;
  for (auto iter = buffer_->get_iter_at_offset(from); iter.get_offset() < start.get_offset();
       ++iter) {
    if (iter.has_tag(tag))
      return true;
  }
  return false;
}

void MainWindow::compact_body_notes()
{
  if (restoring_ || !undo_.is_open())
    return;
  // Nothing to drop, and capture() would copy every paragraph. A marker
  // with no note is left as it is: drop_unmarked_notes is a no-op on an
  // empty list.
  if (notes_.empty())
    return;
  const Document doc = capture();
  const NoteDrop drop = drop_unmarked_notes(doc);
  if (!drop.changed)
    return;
  const std::vector<std::vector<Paragraph>> old_notes = notes_;
  notes_ = drop.notes;
  retitle_note_markers(drop.remap, true, false);
  undo_.suspend(true);
  retitle_note_markers(drop.remap, false, true);
  undo_.suspend(false);
  const std::vector<int> remap = drop.remap;
  const std::vector<int> inverse = inverse_note_remap(remap);
  const std::vector<std::vector<Paragraph>> new_notes = notes_;
  undo_.record_custom(
      [this, old_notes, inverse] {
        notes_ = old_notes;
        retitle_note_markers(inverse, false, true);
        refresh_stories();
        apply_page_size();
      },
      [this, new_notes, remap] {
        notes_ = new_notes;
        retitle_note_markers(remap, false, true);
        refresh_stories();
        apply_page_size();
      });
  refresh_stories();
  apply_page_size();
}

void MainWindow::compact_cell_notes(Document& after)
{
  const NoteDrop drop = drop_unmarked_notes(after);
  if (!drop.changed)
    return;
  apply_note_remap(after, drop.remap);
  after.notes = drop.notes;
  const bool was = restoring_;
  restoring_ = true;
  undo_.suspend(true);
  notes_ = after.notes;
  retitle_note_markers(drop.remap, true, true);
  refresh_stories();
  apply_page_size();
  undo_.suspend(false);
  restoring_ = was;
}

void MainWindow::on_cell_end()
{
  if (!cell_step_)
    return;
  cell_step_ = false;
  Document after = capture();
  if (!(after == cell_before_)) {
    compact_cell_notes(after);
    const gint64 now = g_get_monotonic_time();
    const bool same_cell = cell_edit_.on && cell_edit_.table == merge_cell_table_ &&
                           cell_edit_.row == merge_cell_row_ &&
                           cell_edit_.column == merge_cell_column_;
    const bool continued = last_merge_kind_ == MergeKind::Cell && last_typed_us_ != 0 &&
                           now - last_typed_us_ <= 1000000 && same_cell &&
                           cell_merge_at_ == merge_cell_caret_;
    const Document before = cell_before_;
    const int caret = cursor_offset();
    const auto redo = [this, after, caret] {
      restoring_ = true;
      replace_buffer(after, caret);
      restoring_ = false;
    };
    bool folded = false;
    if (continued)
      folded = undo_.fold_last_custom(redo);
    if (!folded) {
      undo_.record_custom(
          [this, before, caret] {
            restoring_ = true;
            replace_buffer(before, caret);
            restoring_ = false;
          },
          redo);
    }
    last_merge_kind_ = MergeKind::Cell;
    last_typed_us_ = now;
    merge_cell_table_ = cell_edit_.table;
    merge_cell_row_ = cell_edit_.row;
    merge_cell_column_ = cell_edit_.column;
    merge_cell_caret_ = -1;
    if (Gtk::TextView* view = editing_cell_view()) {
      if (view->get_buffer())
        merge_cell_caret_ = view->get_buffer()->get_insert()->get_iter().get_offset();
    }
  }
  close_step(false);
  update_title();
  update_actions();
}

Glib::RefPtr<Gtk::TextTag> MainWindow::heading_tag_on(const Glib::RefPtr<Gtk::TextBuffer>& buffer,
                                                      int level)
{
  if (!buffer)
    return {};
  const Glib::ustring name = "heading-" + std::to_string(level);
  auto table = buffer->get_tag_table();
  auto tag = table->lookup(name);
  if (tag)
    return tag;
  tag = Gtk::TextTag::create(name);
  // Only space around it: a heading's size and weight are its runs', which
  // its style sets, so the page shows what the file holds.
  const double z = std::max(0.5, zoom_factor());
  tag->property_pixels_above_lines() = static_cast<int>(8 * z);
  tag->property_pixels_below_lines() = static_cast<int>(4 * z);
  table->add(tag);
  return tag;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::heading_tag(int level)
{
  return heading_tag_on(buffer_, level);
}

namespace {

// The font's line, in pixels, at this zoom. Line spacing adds to this. It
// does not use the page's twip scale: that scale and Pango's do not match,
// and the gap has to match the line GTK draws.
int line_box_px(const Glib::RefPtr<Pango::Context>& context, double zoom, int basis,
                const std::string& font)
{
  const int half = basis >= 2 ? basis : 22;
  const double factor = zoom > 0 ? zoom : 1.0;
  const double points = (half / 2.0) * factor;
  Pango::FontDescription desc;
  desc.set_family(font.empty() ? "Sans" : font);
  desc.set_size(static_cast<int>(std::lround(points * PANGO_SCALE)));
  if (!context)
    return std::max(1, static_cast<int>(std::lround(points * 96.0 / 72.0)));
  const Pango::FontMetrics metrics = context->get_metrics(desc);
  return std::max(1, (metrics.get_ascent() + metrics.get_descent()) / PANGO_SCALE);
}

}  // namespace

void MainWindow::apply_spacing_pixels(const Glib::RefPtr<Gtk::TextTag>& tag, const Spacing& spacing,
                                      int basis, const std::string& font, int heading,
                                      bool page_break) const
{
  if (!tag)
    return;
  const Spacing clamped = clamp_spacing(spacing);
  const double zoom = std::max(0.5, zoom_factor());
  // get_pango_context() is not const. The metrics do not change the page.
  auto context = const_cast<PageText&>(text_).get_pango_context();
  const int box = line_box_px(context, zoom_factor(), basis, font);
  // At least and Exactly use the font's dpi. The page's twip scale left a
  // 14 pt line on an 11 pt font with no extra. A shorter line stays at the
  // font: GTK cannot draw it tighter.
  const int extra = spacing_line_extra_px(clamped.rule, clamped.value, box, zoom_factor());

  // A page break's 18 replaces the heading's 8. Space before adds to
  // whichever of those is in force. With neither, leave the property unset
  // so a heading tag under this one can still open its own gap.
  const int heading_above = heading > 0 ? static_cast<int>(8 * zoom) : 0;
  int above = 0;
  bool set_above = false;
  if (page_break) {
    above = 18;
    set_above = true;
  }
  if (clamped.before > 0) {
    above += indent_px(clamped.before);
    if (heading > 0 && !page_break)
      above += heading_above;
    set_above = true;
  }
  if (set_above)
    tag->property_pixels_above_lines() = above;
  else
    tag->property_pixels_above_lines_set() = false;

  const int after_px = clamped.after > 0 ? indent_px(clamped.after) : 0;
  if (extra > 0 || after_px > 0) {
    int below = extra + after_px;
    // This tag is raised over the heading, so it has to keep the heading's
    // gap under the paragraph or that gap disappears.
    if (heading > 0)
      below += static_cast<int>(4 * zoom);
    tag->property_pixels_below_lines() = below;
  } else {
    tag->property_pixels_below_lines_set() = false;
  }
  if (extra > 0)
    tag->property_pixels_inside_wrap() = extra;
  else
    tag->property_pixels_inside_wrap_set() = false;
}

void MainWindow::style_cell_para(const Glib::RefPtr<Gtk::TextTag>& tag, const Indents& indents,
                                 Align align, const Spacing& spacing, int basis, int heading,
                                 const std::string& font, const std::vector<TabStop>& tabs) const
{
  if (!tag)
    return;
  if (align == Align::Center)
    tag->property_justification() = Gtk::JUSTIFY_CENTER;
  else if (align == Align::Right)
    tag->property_justification() = Gtk::JUSTIFY_RIGHT;
  else if (align == Align::Justify)
    tag->property_justification() = Gtk::JUSTIFY_FILL;
  else
    tag->property_justification() = Gtk::JUSTIFY_LEFT;
  // No indent leaves the cell view's own padding in charge. A tag margin
  // replaces that padding, the way a body paragraph tag replaces the page's.
  // Spacing still applies when the indents are zero.
  if (indents != Indents{}) {
    const int hang = std::min(0, indents.first);
    tag->property_left_margin() = std::max(0, indent_px(indents.left + hang));
    tag->property_right_margin() = std::max(0, indent_px(indents.right));
    tag->property_indent() = indent_px(indents.first);
  }
  apply_spacing_pixels(tag, spacing, basis, font, heading, false);
  apply_tab_stops(tag, tabs, indents, true, false, zoom_factor());
}

Glib::RefPtr<Gtk::TextTag> MainWindow::cell_para_tag(const Glib::RefPtr<Gtk::TextBuffer>& buffer,
                                                     const Paragraph& paragraph)
{
  if (!buffer)
    return {};
  const int basis = basis_half_points(paragraph);
  const Glib::ustring name =
      cpara_name(paragraph.indents, paragraph.align, paragraph.spacing, basis, paragraph.heading,
                 paragraph.borders, paragraph.shading, paragraph.tabs);
  auto table = buffer->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    // Restyle passes Sans. Using the style's font here would make a zoom
    // restyle jump to a different line.
    style_cell_para(tag, paragraph.indents, paragraph.align, paragraph.spacing, basis,
                    paragraph.heading, "Sans", paragraph.tabs);
    Rgb rgb;
    if (shading_color(paragraph.shading, rgb))
      tag->property_paragraph_background() = rgb_hex(rgb);
    table->add(tag);
  }
  return tag;
}

void MainWindow::tag_cell_paragraph(const Glib::RefPtr<Gtk::TextBuffer>& buffer, int from, int to,
                                    const Paragraph& paragraph)
{
  if (!buffer || from >= to)
    return;
  Glib::RefPtr<Gtk::TextTag> para;
  if (auto tag = cell_para_tag(buffer, paragraph)) {
    buffer->apply_tag(tag, buffer->get_iter_at_offset(from), buffer->get_iter_at_offset(to));
    para = tag;
  }
  if (paragraph.heading >= 1 && paragraph.heading <= 6) {
    auto heading = heading_tag_on(buffer, paragraph.heading);
    if (heading) {
      for (int i = from; i < to;) {
        if (buffer->get_iter_at_offset(i).get_char() == '\n') {
          ++i;
          continue;
        }
        int j = i + 1;
        while (j < to && buffer->get_iter_at_offset(j).get_char() != '\n')
          ++j;
        buffer->apply_tag(heading, buffer->get_iter_at_offset(i), buffer->get_iter_at_offset(j));
        i = j;
      }
    }
  }
  // A new heading tag is added at the top. Spacing has to win those pixels,
  // and it sets no weight, so the heading's look stays.
  if (para && !(paragraph.spacing == Spacing{}))
    para->set_priority(buffer->get_tag_table()->get_size() - 1);
}

Glib::RefPtr<Gtk::TextTag> MainWindow::draft_gap_tag()
{
  const Glib::ustring name = "draft-gap";
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (tag)
    return tag;
  tag = Gtk::TextTag::create(name);
  tag->property_pixels_above_lines() = 18;
  table->add(tag);
  raise_headings();
  return tag;
}

void MainWindow::raise_draft_gap()
{
  if (!buffer_)
    return;
  auto tag = buffer_->get_tag_table()->lookup("draft-gap");
  if (!tag)
    return;
  const int top = buffer_->get_tag_table()->get_size() - 1;
  if (tag->get_priority() < top)
    tag->set_priority(top);
}

bool MainWindow::sync_draft_gaps()
{
  if (!buffer_)
    return false;
  struct Range {
    int start = 0;
    int end = 0;
    bool sheet = false;
  };
  std::vector<Range> ranges;
  const int count = buffer_->get_char_count();
  int offset = 0;
  while (offset <= count) {
    auto iter = buffer_->get_iter_at_offset(offset);
    int next = -1;
    for (auto at = iter; !at.is_end(); ++at) {
      if (at.get_char() == '\n') {
        auto after = at;
        after.forward_char();
        next = after.get_offset();
        break;
      }
    }
    Range range;
    range.start = offset;
    range.end = next < 0 ? count : next;
    range.sheet = starts_sheet(range.start) && range.start < range.end;
    ranges.push_back(range);
    if (next < 0)
      break;
    offset = next;
  }
  const bool want = view_ == ViewMode::Draft;
  bool any = false;
  for (const Range& range : ranges)
    any = any || range.sheet;
  auto table = buffer_->get_tag_table();
  auto gap = table->lookup("draft-gap");
  if ((!want || !any) && !gap)
    return false;
  if (want && any && !gap)
    gap = draft_gap_tag();
  bool changed = false;
  seaming_ = true;
  for (const Range& range : ranges) {
    if (!gap)
      break;
    auto from = buffer_->get_iter_at_offset(range.start);
    auto to = buffer_->get_iter_at_offset(range.end);
    bool applied = false;
    for (const auto& tag : from.get_tags()) {
      if (is_draft_gap(tag_name(tag))) {
        applied = true;
        break;
      }
    }
    if (want && range.sheet) {
      if (!applied) {
        buffer_->apply_tag(gap, from, to);
        changed = true;
      }
    } else if (applied) {
      buffer_->remove_tag(gap, from, to);
      changed = true;
    }
  }
  if ((!want || !any) && gap) {
    table->remove(gap);
    changed = true;
  }
  seaming_ = false;
  return changed;
}

void MainWindow::raise_headings()
{
  // Priorities decide which tags win the indents and margins: lines move.
  label_geometry_.clear();
  auto table = buffer_->get_tag_table();
  for (int level = 1; level <= 6; ++level) {
    auto tag = table->lookup("heading-" + std::to_string(level));
    if (tag)
      tag->set_priority(table->get_size() - 1);
  }
  // A paragraph's own spacing sits above its heading, or the heading's gap
  // hides the space before. A page break alone is not raised: its 18 stays
  // under the heading. List items are not raised either: their shift tag has
  // to stay above the paragraph's margin, and a tag's priority is one number.
  std::vector<Glib::RefPtr<Gtk::TextTag>> spaced;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    ParaFormat format;
    if (!parse_para(tag_name(tag), format))
      return;
    if (format.list.kind != ListKind::None || format.spacing == Spacing{})
      return;
    spaced.push_back(tag);
  });
  for (const auto& tag : spaced)
    tag->set_priority(table->get_size() - 1);
  // A heading that is also a list item keeps its space on a tag of its own,
  // above the heading and under the draft gap. The list tag stays put: its
  // shift tag has to win the margin.
  std::vector<Glib::RefPtr<Gtk::TextTag>> gaps;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (is_spacing_gap(tag_name(tag)))
      gaps.push_back(tag);
  });
  for (const auto& tag : gaps)
    tag->set_priority(table->get_size() - 1);
  // A seam has to outrank a heading, or the heading's own space wins and the
  // gap between sheets never opens. Draft has no seam. Its gap sits above the
  // heading and under the seam, and it sets no indent. Spacing stays under
  // that gap, so a sheet's top still suppresses space before.
  raise_draft_gap();
  raise_seams();
}

void MainWindow::restyle_tags()
{
  const double z = zoom_factor();
  buffer_->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    Run run;
    if (parse_fmt(tag_name(tag), run))
      tag->property_size_points() = run.size * z;
    const int level = heading_level_name(tag_name(tag));
    if (level > 0) {
      tag->property_pixels_above_lines() = static_cast<int>(8 * z);
      tag->property_pixels_below_lines() = static_cast<int>(4 * z);
    }
    ParaFormat format;
    if (parse_para(tag_name(tag), format))
      style_para_tag(tag, format);
    if (!is_spacing_gap(tag_name(tag)))
      return;
    const std::string name = tag_name(tag);
    const std::string prefix = std::string("gap") + '\x1f';
    const size_t mark = name.find('\x1f', prefix.size());
    if (mark == std::string::npos || !parse_para(name.substr(mark + 1), format))
      return;
    int gap_level = 0;
    try {
      gap_level = std::stoi(name.substr(prefix.size(), mark - prefix.size()));
    } catch (const std::exception&) {
      gap_level = 0;
    }
    std::string font = "Sans";
    if (const Style* style = find_style(styles_, format.style)) {
      if (!style->format.font.empty())
        font = style->format.font;
      gap_level = std::max(gap_level, style->heading);
    }
    apply_spacing_pixels(tag, format.spacing, format.basis, font, gap_level, false);
  });
  // Zoom, the view, and the margins move labels, their room, and tab stops.
  tabs_full_ = true;
  tab_widths_.clear();
  queue_list_shifts();
  queue_list_tabs();
  caret_key_.clear();
  update_caret_font();
  relayout_tables();
  apply_story_zoom();
  rescale_pictures();
}

ViewGeometry MainWindow::geometry() const
{
  return view_geometry(view_, zoom_factor(), page_setup_);
}

void MainWindow::set_view(ViewMode mode)
{
  if (mode == view_)
    return;
  view_ = mode;
  // Draft drops the sheet, its shadow and the gray pasteboard; the CSS
  // keys off one class on each of them.
  const ViewGeometry g = geometry();
  for (Gtk::Widget* widget :
       {static_cast<Gtk::Widget*>(&paste_), static_cast<Gtk::Widget*>(&board_),
        static_cast<Gtk::Widget*>(&page_)}) {
    if (g.chrome)
      widget->get_style_context()->remove_class("draft");
    else
      widget->get_style_context()->add_class("draft");
  }
  page_.set_halign(g.centred ? Gtk::ALIGN_CENTER : Gtk::ALIGN_START);
  // Draft's white area runs the full height, the text from the top.
  page_.set_valign(g.chrome ? Gtk::ALIGN_START : Gtk::ALIGN_FILL);
  if (g.chrome)
    page_.property_vexpand_set() = false;  // back to the default Page layout
  else
    page_.set_vexpand(true);
  page_.set_margin_top(g.gap);
  page_.set_margin_bottom(g.gap);
  refresh_stories();
  apply_page_size();
  ruler_.queue_draw();
  text_.grab_focus();
}

int MainWindow::margin_left() const
{
  return geometry().margin_left;
}

int MainWindow::margin_right() const
{
  return geometry().margin_right;
}

int MainWindow::indent_px(int twips) const
{
  return twips_to_px(twips, zoom_factor());
}

void MainWindow::apply_margins()
{
  const ViewGeometry g = geometry();
  const int left = g.margin_left;
  const int right = g.margin_right;
  const int y = g.margin_y;
  const int bottom = g.margin_bottom;
  if (text_.get_left_margin() != left)
    text_.set_left_margin(left);
  if (text_.get_right_margin() != right)
    text_.set_right_margin(right);
  if (text_.get_top_margin() != y)
    text_.set_top_margin(y);
  if (text_.get_bottom_margin() != bottom)
    text_.set_bottom_margin(bottom);
}

double MainWindow::zoom_factor() const
{
  if (settings_.zoom == 0) {
    const int width = std::max(120, paste_.get_allocated_width() - 36);
    const int page = std::max(1, view_geometry(ViewMode::Page, 1.0, page_setup_).page_width);
    return static_cast<double>(width) / static_cast<double>(page);
  }
  return static_cast<double>(settings_.zoom) / 100.0;
}

double MainWindow::story_zoom() const
{
  return std::min(zoom_factor(), 1.0);
}

void MainWindow::queue_story_tabs()
{
  if (closing_ || story_tabs_idle_.connected() || story_tabs_zoom_ == story_zoom())
    return;
  story_tabs_idle_ = Glib::signal_idle().connect([this] {
    story_tabs_idle_.disconnect();
    if (closing_ || story_tabs_zoom_ == story_zoom())
      return false;
    story_tabs_zoom_ = story_zoom();
    refresh_stories();
    return false;
  });
}

Run MainWindow::format_of(const Gtk::TextIter& iter) const
{
  bool found = false;
  int best = -1;
  Run chosen;
  for (const auto& tag : iter.get_tags()) {
    Run run;
    if (!parse_fmt(tag_name(tag), run))
      continue;
    const int priority = tag->get_priority();
    if (!found || priority > best) {
      found = true;
      best = priority;
      chosen = run;
    }
  }
  if (found)
    return chosen;
  Run run;
  run.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  run.size = known_size(settings_.default_size) ? settings_.default_size : 11;
  return run;
}

int MainWindow::heading_of(const Gtk::TextIter& iter) const
{
  int found = 0;
  int best = -1;
  for (const auto& tag : iter.get_tags()) {
    const int level = heading_level_name(tag_name(tag));
    if (level <= 0)
      continue;
    const int priority = tag->get_priority();
    if (priority > best) {
      best = priority;
      found = level;
    }
  }
  return found;
}

int MainWindow::heading_near(int offset) const
{
  if (offset > 0) {
    auto prev = buffer_->get_iter_at_offset(offset - 1);
    if (prev.get_char() != '\n') {
      const int level = heading_of(prev);
      if (level > 0)
        return level;
    }
  }
  for (auto iter = buffer_->get_iter_at_offset(offset); !iter.is_end() && iter.get_char() != '\n';
       ++iter) {
    const int level = heading_of(iter);
    if (level > 0)
      return level;
  }
  return 0;
}

bool MainWindow::has_fmt(const Gtk::TextIter& iter) const
{
  for (const auto& tag : iter.get_tags()) {
    Run run;
    if (parse_fmt(tag_name(tag), run))
      return true;
  }
  return false;
}

void MainWindow::strip_fmt(const Gtk::TextIter& from, const Gtk::TextIter& to)
{
  std::vector<Glib::RefPtr<Gtk::TextTag>> tags;
  for (auto iter = from; iter != to; ++iter) {
    for (const auto& tag : iter.get_tags()) {
      Run ignored;
      if (!parse_fmt(tag_name(tag), ignored))
        continue;
      if (std::find(tags.begin(), tags.end(), tag) == tags.end())
        tags.push_back(tag);
    }
  }
  for (const auto& tag : tags)
    buffer_->remove_tag(tag, from, to);
}

void MainWindow::set_fmt_on(const Glib::RefPtr<Gtk::TextBuffer>& buffer, const Gtk::TextIter& from,
                            const Gtk::TextIter& to, const Glib::RefPtr<Gtk::TextTag>& tag)
{
  if (!buffer || !tag)
    return;
  // Offsets, since removing tags can leave the iterators invalid.
  const int a = from.get_offset();
  const int b = to.get_offset();
  std::vector<Glib::RefPtr<Gtk::TextTag>> tags;
  for (auto iter = buffer->get_iter_at_offset(a); iter != buffer->get_iter_at_offset(b); ++iter) {
    for (const auto& existing : iter.get_tags()) {
      Run ignored;
      if (!parse_fmt(tag_name(existing), ignored))
        continue;
      if (std::find(tags.begin(), tags.end(), existing) == tags.end())
        tags.push_back(existing);
    }
  }
  for (const auto& existing : tags)
    buffer->remove_tag(existing, buffer->get_iter_at_offset(a), buffer->get_iter_at_offset(b));
  buffer->apply_tag(tag, buffer->get_iter_at_offset(a), buffer->get_iter_at_offset(b));
}

void MainWindow::set_fmt(const Gtk::TextIter& from, const Gtk::TextIter& to,
                         const Glib::RefPtr<Gtk::TextTag>& tag)
{
  set_fmt_on(buffer_, from, to, tag);
}

void MainWindow::on_tag_applied(const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& from,
                                const Gtk::TextIter& to)
{
  if (!pending_insert_ || !in_user_ || loading_ || restoring_)
    return;
  Run run;
  ParaFormat para;
  const std::string name = tag_name(tag);
  const bool fmt = parse_fmt(name, run);
  if (!fmt && !parse_para(name, para))
    return;
  // Text inserted later at either end is not inside this range.
  (fmt ? applied_fmt_ : applied_para_)
      .push_back({tag, buffer_->create_mark(from, false), buffer_->create_mark(to, true)});
}

void MainWindow::clear_applied_fmt()
{
  for (auto* list : {&applied_fmt_, &applied_para_}) {
    for (const AppliedFmt& rec : *list) {
      buffer_->delete_mark(rec.from);
      buffer_->delete_mark(rec.to);
    }
    list->clear();
  }
}

Run MainWindow::line_break_mark(int newline) const
{
  int begin = newline;
  while (begin > 0 && buffer_->get_iter_at_offset(begin - 1).get_char() != '\n')
    --begin;
  bool any = false;
  Run mark;
  for (int k = begin; k < newline; ++k) {
    auto iter = buffer_->get_iter_at_offset(k);
    if (iter.get_char() == '\n')
      continue;
    Run run = format_of(iter);
    run.text.clear();
    if (!any || run.size > mark.size) {
      mark = run;
      any = true;
    }
  }
  if (!any) {
    mark.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
    mark.size = known_size(settings_.default_size) ? settings_.default_size : 11;
    return mark;
  }
  if (mark.font.empty())
    mark.font = "Sans";
  if (!known_size(mark.size))
    mark.size = 11;
  return mark;
}

void MainWindow::tag_line_breaks(int start, int end)
{
  // The newline's font is part of the line box. Pin a line that has text to
  // that text's size. An untagged newline uses the widget font, which stays
  // on the document default, so blank lines do not follow the caret.
  if (!buffer_)
    return;
  const int count = buffer_->get_char_count();
  if (count == 0)
    return;
  start = std::max(0, std::min(start, count));
  end = std::max(start, std::min(end, count));
  int from = start;
  while (from > 0 && buffer_->get_iter_at_offset(from - 1).get_char() != '\n')
    --from;
  int to = end;
  if (to > 0 && buffer_->get_iter_at_offset(to - 1).get_char() != '\n') {
    while (to < count && buffer_->get_iter_at_offset(to).get_char() != '\n')
      ++to;
    if (to < count)
      ++to;
  }
  for (int n = from; n < to; ++n) {
    if (buffer_->get_iter_at_offset(n).get_char() != '\n')
      continue;
    int begin = n;
    while (begin > 0 && buffer_->get_iter_at_offset(begin - 1).get_char() != '\n')
      --begin;
    // An empty line keeps the format its newline has: its own, from the
    // file, from the line it was split from, or from typing (finish_pending).
    // An untagged one stays at the document default.
    if (begin == n)
      continue;
    auto from_it = buffer_->get_iter_at_offset(n);
    auto to_it = buffer_->get_iter_at_offset(n + 1);
    set_fmt(from_it, to_it, format_tag(line_break_mark(n)));
  }
}

void MainWindow::apply_run_edit(const std::function<void(Run&)>& edit)
{
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  if (!buffer_->get_selection_bounds(start_iter, end_iter) || start_iter == end_iter) {
    edit(typing_);
    show_format(typing_);
    text_.grab_focus();
    return;
  }
  const int start = start_iter.get_offset();
  const int end = end_iter.get_offset();
  buffer_->begin_user_action();
  for (int i = start; i < end;) {
    auto iter = buffer_->get_iter_at_offset(i);
    if (iter.get_char() == '\n') {
      // A selected empty line takes the change on its newline, which holds
      // its format (Paragraph::mark), as Word's paragraph mark does.
      if (i == 0 || buffer_->get_iter_at_offset(i - 1).get_char() == '\n') {
        Run mark = format_of(iter);
        edit(mark);
        set_fmt(iter, buffer_->get_iter_at_offset(i + 1), format_tag(mark));
      }
      ++i;
      continue;
    }
    Run run = format_of(iter);
    int j = i + 1;
    while (j < end) {
      auto next = buffer_->get_iter_at_offset(j);
      if (next.get_char() == '\n' || !same_format(format_of(next), run))
        break;
      ++j;
    }
    edit(run);
    auto from = buffer_->get_iter_at_offset(i);
    auto to = buffer_->get_iter_at_offset(j);
    set_fmt(from, to, format_tag(run));
    i = j;
  }
  tag_line_breaks(start, end);
  buffer_->end_user_action();
  buffer_->select_range(buffer_->get_iter_at_offset(start), buffer_->get_iter_at_offset(end));
  show_format(format_of(buffer_->get_iter_at_offset(start)));
  text_.grab_focus();
}

bool MainWindow::text_flag(const Run& run, TextFlag flag)
{
  switch (flag) {
    case TextFlag::Bold:
      return run.bold;
    case TextFlag::Italic:
      return run.italic;
    case TextFlag::Underline:
      return run.underline;
  }
  return false;
}

void MainWindow::set_text_flag(Run& run, TextFlag flag, bool on)
{
  switch (flag) {
    case TextFlag::Bold:
      run.bold = on;
      run.direct |= kDirectBold;
      break;
    case TextFlag::Italic:
      run.italic = on;
      run.direct |= kDirectItalic;
      break;
    case TextFlag::Underline:
      run.underline = on;
      run.direct |= kDirectUnderline;
      break;
  }
}

void MainWindow::toggle_flag(TextFlag flag)
{
  if (suppress_format_)
    return;
  if (editing_cell()) {
    toggle_cell_flag(flag);
    return;
  }
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  if (!buffer_->get_selection_bounds(start_iter, end_iter) || start_iter == end_iter) {
    set_text_flag(typing_, flag, !text_flag(typing_, flag));
    show_format(typing_);
    text_.grab_focus();
    return;
  }
  bool all = true;
  bool any = false;
  for (auto iter = start_iter; iter != end_iter; ++iter) {
    // A newline counts only for an empty line, whose format it holds
    // (Paragraph::mark): in Word, the paragraph mark is bolded too.
    if (iter.get_char() == '\n' && !iter.starts_line())
      continue;
    any = true;
    if (!text_flag(format_of(iter), flag)) {
      all = false;
      break;
    }
  }
  if (!any) {
    text_.grab_focus();
    return;
  }
  const bool turn_on = !all;
  apply_run_edit([flag, turn_on](Run& run) { set_text_flag(run, flag, turn_on); });
}

void MainWindow::show_format(const Run& run)
{
  suppress_format_ = true;
  const Glib::ustring font = run.font.empty() ? "Sans" : run.font;
  if (font_combo_.get_active_text() != font) {
    font_combo_.set_active_text(font);
    if (font_combo_.get_active_text() != font) {
      font_combo_.append(font);
      font_combo_.set_active_text(font);
    }
  }
  // The caret's own size, listed among the presets when it is not one.
  const std::vector<double> choices = size_choices(run.size);
  if (choices != size_choices_shown_) {
    size_combo_.remove_all();
    for (double choice : choices)
      size_combo_.append(size_text(choice));
    size_choices_shown_ = choices;
  }
  show_size(run.size);
  if (bold_toggle_ && bold_toggle_->get_active() != run.bold)
    bold_toggle_->set_active(run.bold);
  if (italic_toggle_ && italic_toggle_->get_active() != run.italic)
    italic_toggle_->set_active(run.italic);
  if (underline_toggle_ && underline_toggle_->get_active() != run.underline)
    underline_toggle_->set_active(run.underline);
  suppress_format_ = false;
  update_caret_font();
}

void MainWindow::show_size(double run_size)
{
  // Half points as Word 97 shows them: 10.5. The entry's own text is not
  // enough: a size typed and applied leaves no list item active, and the
  // list then highlighted its first, 8. The item for the size is made
  // active, so the list highlights it.
  const Glib::ustring size = size_text(run_size);
  Glib::ustring active;
  if (auto row = size_combo_.get_active())
    row->get_value(0, active);
  if (active == size && size_combo_.get_active_text() == size)
    return;
  const bool guard = suppress_format_;
  suppress_format_ = true;
  size_combo_.set_active_text(size);
  // A size the list lacks highlights nothing, and the box still says it.
  if (size_combo_.get_active_row_number() < 0) {
    if (Gtk::Entry* entry = size_combo_.get_entry())
      entry->set_text(size);
  }
  suppress_format_ = guard;
}

Run MainWindow::style_typing(const std::string& name) const
{
  Run run;
  // sheet() is a temporary. find_style's pointer has to outlive that call,
  // so the vector stays in scope until the format is copied.
  const std::vector<Style> styles = sheet();
  if (const Style* style = find_style(styles, name))
    run = style->format;
  if (run.font.empty())
    run.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  if (!known_size(run.size))
    run.size = known_size(settings_.default_size) ? settings_.default_size : 11;
  run.text.clear();
  run.direct = 0;
  run.note = 0;
  run.image.reset();
  return run;
}

void MainWindow::sync_format_controls()
{
  if (loading_ || restoring_ || suppress_format_ || in_user_ || !buffer_)
    return;
  if (editing_cell()) {
    ruler_.queue_draw();
    show_cell_format();
    show_align();
    sync_list_controls();
    return;
  }
  if (story_target_ != 0) {
    ruler_.queue_draw();
    show_align();
    sync_list_controls();
    return;
  }
  ruler_.queue_draw();
  show_align();
  sync_list_controls();
  show_style();
  auto iter = buffer_->get_insert()->get_iter();
  if (iter.get_offset() > 0) {
    auto prev = iter;
    if (prev.backward_char() && prev.get_char() != '\n') {
      typing_ = format_of(prev);
      show_format(typing_);
      return;
    }
  }
  if (!iter.is_end() && (iter.get_char() != '\n' || has_fmt(iter))) {
    typing_ = format_of(iter);
    show_format(typing_);
    return;
  }
  // An empty last paragraph's own format, from the file.
  if (iter.is_end() && pending_mark_set_ && final_paragraph_empty()) {
    typing_ = pending_mark_;
    show_format(typing_);
    return;
  }
  // An empty paragraph in a style other than Normal types in that style, as
  // in Word; in Normal, what was last typed carries on, as before styles.
  const std::vector<Style> styles = sheet();
  const Style* style = find_style(styles, para_at(iter.get_offset()).style);
  if (style && style != &styles.front()) {
    typing_ = style->format;
    typing_.text.clear();
  }
  show_format(typing_);
}

Pango::FontDescription MainWindow::story_font() const
{
  const std::string font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  const int size = known_size(settings_.default_size) ? settings_.default_size : 11;
  Pango::FontDescription desc;
  desc.set_family(font);
  desc.set_size(static_cast<int>(size * zoom_factor() * PANGO_SCALE));
  return desc;
}

void MainWindow::use_story_css(Gtk::Widget& widget)
{
  if (!story_css_)
    story_css_ = Gtk::CssProvider::create();
  auto context = widget.get_style_context();
  if (!context)
    return;
  context->remove_provider(story_css_);
  context->add_provider(story_css_, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void MainWindow::apply_story_zoom()
{
  // The body keeps growing past 100%. Notes, the header, and the footer
  // stop there: the full factor drew them as large as the zoomed body.
  const double zoom = std::min(zoom_factor(), 1.0);
  const int margin = std::max(1, static_cast<int>(std::lround(8.0 * zoom)));
  const int mark = std::max(1, static_cast<int>(std::lround(6.0 * zoom)));
  const std::string family_raw = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  std::string family;
  for (char c : family_raw) {
    if (c != '"' && c != '\\' && c != '\n' && c != '\r')
      family.push_back(c);
  }
  if (family.empty())
    family = "Sans";
  const int size = known_size(settings_.default_size) ? settings_.default_size : 11;
  char points[32];
  std::snprintf(points, sizeof points, "%.4f", static_cast<double>(size) * zoom);
  const std::string css = "* { font-family: \"" + family + "\"; font-size: " + points + "pt; }\n";
  if (!story_css_)
    story_css_ = Gtk::CssProvider::create();
  try {
    story_css_->load_from_data(css);
  } catch (const Glib::Error&) {
  }
  auto face = [&](Gtk::TextView& view) {
    use_story_css(view);
    if (view.get_left_margin() != margin)
      view.set_left_margin(margin);
    if (view.get_right_margin() != margin)
      view.set_right_margin(margin);
  };
  auto numbers = [&](Gtk::Label& first, const std::vector<Gtk::Label*>& labels, Gtk::Fixed& marks) {
    use_story_css(first);
    for (Gtk::Label* label : labels) {
      if (label)
        use_story_css(*label);
    }
    if (marks.get_margin_right() != mark)
      marks.set_margin_right(mark);
  };
  face(header_view_);
  face(footer_view_);
  face(notes_view_);
  numbers(note_label_, note_numbers_, note_marks_);
  for (const auto& band : note_bands_) {
    if (!band || !band->wired)
      continue;
    face(band->view);
    numbers(band->label, band->numbers, band->marks);
  }
  // The gutter just changed with the mark margin. Rewrap to the new column.
  fit_note_views();
  // Tab pixels follow the capped zoom. Retagging here runs from a size
  // allocation, so the story tags wait for an idle.
  queue_story_tabs();
}

void MainWindow::update_caret_font()
{
  // A blank line with no format of its own (an untagged newline) renders in
  // the widget font. Keep that font on the document default. Following the caret resized
  // every blank line whenever the selection moved.
  const std::string font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  const int size = known_size(settings_.default_size) ? settings_.default_size : 11;
  const int milli = static_cast<int>(size * zoom_factor() * 100.0);
  const std::string key = font + ":" + std::to_string(milli);
  if (key == caret_key_)
    return;
  caret_key_ = key;
  text_.override_font(story_font());
}

Glib::RefPtr<Gdk::Pixbuf> ink_pixbuf(Ink ink)
{
  auto pix = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, 16, 16);
  if (!pix)
    return {};
  const Rgb c = ink == Ink::Auto ? Rgb{255, 255, 255} : ink_rgb(ink);
  pix->fill((static_cast<guint32>(std::max(0, std::min(255, c.red))) << 24) |
            (static_cast<guint32>(std::max(0, std::min(255, c.green))) << 16) |
            (static_cast<guint32>(std::max(0, std::min(255, c.blue))) << 8) | 0xFFu);
  return pix;
}

void MainWindow::paint_ink(Gtk::MenuToolButton* button, Ink ink)
{
  if (!button)
    return;
  if (auto* image = dynamic_cast<Gtk::Image*>(button->get_icon_widget()))
    image->set(ink_pixbuf(ink));
}

void MainWindow::apply_font_color(Ink ink)
{
  font_ink_ = ink_clamp(static_cast<int>(ink));
  paint_ink(font_color_button_, font_ink_);
  auto edit = [ink = font_ink_](Run& run) {
    if (run.color == ink)
      return;
    run.color = ink;
    run.direct |= kDirectColor;
  };
  if (editing_cell())
    edit_cell_runs(edit);
  else
    apply_run_edit(edit);
}

void MainWindow::apply_highlight(Ink ink)
{
  highlight_ink_ = ink_clamp(static_cast<int>(ink));
  paint_ink(highlight_button_, highlight_ink_);
  auto edit = [ink = highlight_ink_](Run& run) {
    if (run.highlight == ink)
      return;
    run.highlight = ink;
    run.direct |= kDirectHighlight;
  };
  if (editing_cell())
    edit_cell_runs(edit);
  else
    apply_run_edit(edit);
}

void MainWindow::on_font_dialog()
{
  const Run shown = dialog_run();
  Gtk::Dialog dialog("Font", *this, true);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_column_spacing(8);
  grid->set_row_spacing(6);
  grid->set_margin_top(8);
  grid->set_margin_bottom(8);
  grid->set_margin_start(8);
  grid->set_margin_end(8);

  auto* font_label = Gtk::manage(new Gtk::Label("Font:", Gtk::ALIGN_START));
  auto* font = Gtk::manage(new Gtk::ComboBoxText());
  fill_font_combo(*font, shown.font.empty() ? "Sans" : shown.font);
  auto* size_label = Gtk::manage(new Gtk::Label("Size:", Gtk::ALIGN_START));
  auto* size = Gtk::manage(new Gtk::ComboBoxText(true));
  for (double choice : size_choices(shown.size))
    size->append(size_text(choice));
  size->set_active_text(size_text(shown.size));
  auto* color_label = Gtk::manage(new Gtk::Label("Color:", Gtk::ALIGN_START));
  auto* color = Gtk::manage(new Gtk::ComboBoxText());
  for (int i = 0; i <= 16; ++i)
    color->append(ink_name(static_cast<Ink>(i), false));
  color->set_active(static_cast<int>(ink_clamp(static_cast<int>(shown.color))));

  auto* bold = Gtk::manage(new Gtk::CheckButton("Bold"));
  auto* italic = Gtk::manage(new Gtk::CheckButton("Italic"));
  auto* underline = Gtk::manage(new Gtk::CheckButton("Underline"));
  auto* strike = Gtk::manage(new Gtk::CheckButton("Strikethrough"));
  auto* super = Gtk::manage(new Gtk::CheckButton("Superscript"));
  auto* sub = Gtk::manage(new Gtk::CheckButton("Subscript"));
  bold->set_active(shown.bold);
  italic->set_active(shown.italic);
  underline->set_active(shown.underline);
  strike->set_active(shown.strike);
  super->set_active(shown.script == Script::Super);
  sub->set_active(shown.script == Script::Sub);
  bool guard = false;
  super->signal_toggled().connect([&] {
    if (guard)
      return;
    guard = true;
    if (super->get_active())
      sub->set_active(false);
    guard = false;
  });
  sub->signal_toggled().connect([&] {
    if (guard)
      return;
    guard = true;
    if (sub->get_active())
      super->set_active(false);
    guard = false;
  });

  grid->attach(*font_label, 0, 0, 1, 1);
  grid->attach(*font, 1, 0, 1, 1);
  grid->attach(*size_label, 0, 1, 1, 1);
  grid->attach(*size, 1, 1, 1, 1);
  grid->attach(*color_label, 0, 2, 1, 1);
  grid->attach(*color, 1, 2, 1, 1);
  grid->attach(*bold, 0, 3, 2, 1);
  grid->attach(*italic, 0, 4, 2, 1);
  grid->attach(*underline, 0, 5, 2, 1);
  grid->attach(*strike, 0, 6, 2, 1);
  grid->attach(*super, 0, 7, 2, 1);
  grid->attach(*sub, 0, 8, 2, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all_children();

  while (dialog.run() == Gtk::RESPONSE_OK) {
    const std::string refusal = size_refusal(size->get_active_text());
    if (!refusal.empty()) {
      tell(refusal);
      continue;
    }
    const std::string face = font->get_active_text();
    const double chosen_size = parse_size(size->get_active_text());
    const Ink chosen_color = ink_clamp(color->get_active_row_number());
    const bool want_bold = bold->get_active();
    const bool want_italic = italic->get_active();
    const bool want_underline = underline->get_active();
    const bool want_strike = strike->get_active();
    Script script = Script::None;
    if (super->get_active())
      script = Script::Super;
    else if (sub->get_active())
      script = Script::Sub;
    auto edit = [=](Run& run) {
      if (!face.empty() && run.font != face) {
        run.font = face;
        run.direct |= kDirectFont;
      }
      if (known_size(chosen_size) && run.size != chosen_size) {
        run.size = chosen_size;
        run.direct |= kDirectSize;
      }
      if (run.bold != want_bold) {
        run.bold = want_bold;
        run.direct |= kDirectBold;
      }
      if (run.italic != want_italic) {
        run.italic = want_italic;
        run.direct |= kDirectItalic;
      }
      if (run.underline != want_underline) {
        run.underline = want_underline;
        run.direct |= kDirectUnderline;
      }
      if (run.color != chosen_color) {
        run.color = chosen_color;
        run.direct |= kDirectColor;
      }
      if (run.strike != want_strike) {
        run.strike = want_strike;
        run.direct |= kDirectStrike;
      }
      if (run.script != script) {
        run.script = script;
        run.direct |= kDirectScript;
      }
    };
    if (editing_cell())
      edit_cell_runs(edit);
    else
      apply_run_edit(edit);
    break;
  }
}

void MainWindow::on_font_changed()
{
  if (suppress_format_)
    return;
  const Glib::ustring name = font_combo_.get_active_text();
  if (name.empty())
    return;
  auto edit = [name](Run& run) {
    run.font = name.raw();
    run.direct |= kDirectFont;
  };
  if (editing_cell()) {
    edit_cell_runs(edit);
    return;
  }
  apply_run_edit(edit);
}

void MainWindow::on_size_changed()
{
  if (suppress_format_)
    return;
  // A size being typed takes effect on Enter (on_size_entered()), not at
  // each keystroke: "10.5" is not 1 pt, then 10 pt, then 10.5.
  if (size_combo_.get_has_entry() && size_combo_.get_active_row_number() < 0)
    return;
  const double size = parse_size(size_combo_.get_active_text().raw());
  if (size == 0)
    return;
  auto edit = [size](Run& run) {
    run.size = size;
    run.direct |= kDirectSize;
  };
  if (editing_cell()) {
    edit_cell_runs(edit);
    return;
  }
  apply_run_edit(edit);
}

void MainWindow::on_size_entered()
{
  if (suppress_format_)
    return;
  const std::string typed = size_combo_.get_active_text().raw();
  const std::string refusal = size_refusal(typed);
  if (refusal.empty()) {
    const double size = parse_size(typed);
    auto edit = [size](Run& run) {
      run.size = size;
      run.direct |= kDirectSize;
    };
    if (editing_cell())
      edit_cell_runs(edit);
    else
      apply_run_edit(edit);
    return;
  }
  // Not a size Word takes (10.3, 2000, "big"). As Word 97 does, say why,
  // then show the size of the text, which keeps it, selected in the box to
  // type over. A cell keeps the entry: restoring the cell would take it.
  Gtk::MessageDialog dialog(*this, refusal, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
  dialog.set_title("Write-It");
  dialog.run();
  dialog.hide();
  if (editing_cell()) {
    show_cell_format();
  } else {
    Gtk::TextBuffer::iterator start_iter;
    Gtk::TextBuffer::iterator end_iter;
    if (buffer_->get_selection_bounds(start_iter, end_iter) && start_iter != end_iter)
      show_format(format_of(start_iter));
    else
      show_format(typing_);
  }
  if (Gtk::Entry* entry = size_combo_.get_entry()) {
    entry->grab_focus();
    entry->select_region(0, -1);
  }
}

void MainWindow::apply_tab_stops(const Glib::RefPtr<Gtk::TextTag>& tag,
                                 const std::vector<TabStop>& tabs, const Indents& indents,
                                 bool cell, bool list, double zoom) const
{
  if (!tag)
    return;
  // Pango measures from the layout origin, which is the tag's left margin.
  // That margin already includes a hang, so a stop's pixel is its distance
  // from the same origin. A bar is drawn, not given to Pango.
  const int origin = !cell && list ? std::max(0, indents.left)
                                   : std::max(0, indents.left + std::min(0, indents.first));
  const int interval = clamp_default_tab(default_tab_);
  const std::vector<TabStop> custom = clamp_tabs(tabs);
  struct Placed {
    int px;
    PangoTabAlign align;
  };
  std::vector<Placed> placed;
  int last = -1;
  auto place = [&](int position, PangoTabAlign align) {
    const int px = twips_to_px(position, zoom) - twips_to_px(origin, zoom);
    if (px <= 0)
      return;
    if (!placed.empty() && px <= placed.back().px)
      return;
    placed.push_back(Placed{px, align});
  };
  for (const TabStop& stop : custom) {
    last = stop.position;
    if (stop.align == TabAlign::Bar)
      continue;
    PangoTabAlign align = PANGO_TAB_LEFT;
    if (stop.align == TabAlign::Center)
      align = PANGO_TAB_CENTER;
    else if (stop.align == TabAlign::Right)
      align = PANGO_TAB_RIGHT;
    else if (stop.align == TabAlign::Decimal)
      align = PANGO_TAB_DECIMAL;
    place(stop.position, align);
  }
  int pos = interval;
  if (!custom.empty()) {
    pos = (last / interval + 1) * interval;
    if (pos <= last)
      pos += interval;
  }
  const int reach =
      std::max(page_text_twips(page_setup_), indents.left + std::max(0, indents.first));
  while (static_cast<int>(placed.size()) < 64 && pos <= reach && pos < kMaxIndent) {
    place(pos, PANGO_TAB_LEFT);
    pos += interval;
  }
  if (placed.empty()) {
    tag->property_tabs_set() = false;
    return;
  }
  Pango::TabArray array(static_cast<int>(placed.size()), true);
  for (int i = 0; i < static_cast<int>(placed.size()); ++i)
    pango_tab_array_set_tab(array.gobj(), i, placed[static_cast<size_t>(i)].align,
                            placed[static_cast<size_t>(i)].px);
  tag->property_tabs() = array;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::para_tag(const ParaFormat& raw)
{
  ParaFormat format;
  format.indents = clamp_indents(raw.indents);
  format.align = raw.align;
  format.list = clamp_list(raw.list);
  format.style = raw.style;
  format.direct = raw.direct;
  format.page_break = raw.page_break;
  format.cell = raw.cell;
  format.cell.borders = clamp_borders(raw.cell.borders);
  format.cell.shading = clamp_shading(raw.cell.shading);
  format.spacing = clamp_spacing(raw.spacing);
  format.basis = raw.basis >= 2 ? raw.basis : 22;
  format.borders = clamp_borders(raw.borders);
  format.shading = clamp_shading(raw.shading);
  format.tabs = clamp_tabs(raw.tabs);
  const Glib::ustring name = para_name(format);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    style_para_tag(tag, format);
    table->add(tag);
    // A new tag sits at the top. Raise it under the draft gap once the
    // document is on screen. Loading raises every tag together at the end.
    if (!loading_ && format.list.kind == ListKind::None && !(format.spacing == Spacing{}))
      raise_headings();
  }
  return tag;
}

void MainWindow::style_para_tag(const Glib::RefPtr<Gtk::TextTag>& tag,
                                const ParaFormat& format) const
{
  if (format.cell.table != 0)
    tag->property_paragraph_background() = "#f4f4f4";
  else {
    Rgb rgb;
    if (shading_color(format.shading, rgb))
      tag->property_paragraph_background() = rgb_hex(rgb);
  }
  if (format.align == Align::Center)
    tag->property_justification() = Gtk::JUSTIFY_CENTER;
  else if (format.align == Align::Right)
    tag->property_justification() = Gtk::JUSTIFY_RIGHT;
  else if (format.align == Align::Justify)
    tag->property_justification() = Gtk::JUSTIFY_FILL;
  const Indents& indents = format.indents;
  if (format.list.kind != ListKind::None) {
    // A list item's label is drawn in the hang (on_text_draw), so its first
    // line of text starts past the label rather than out in the hang.
    tag->property_left_margin() = std::max(0, margin_left() + indent_px(indents.left));
    tag->property_right_margin() = margin_right() + indent_px(indents.right);
    tag->property_indent() = indent_px(list_text_start(indents)) - indent_px(indents.left);
  } else if (!(indents == Indents{})) {
    // No indent leaves the view's page margins in charge. A tag's margin
    // replaces the view's, so the page margin is added here. GTK hangs a
    // negative indent from the first line; Word hangs the first line out
    // from the rest. Moving the margin left by the hang lines them up.
    const int hang = std::min(0, indents.first);
    tag->property_left_margin() = std::max(0, margin_left() + indent_px(indents.left + hang));
    tag->property_right_margin() = margin_right() + indent_px(indents.right);
    tag->property_indent() = indent_px(indents.first);
  }
  // Spacing still applies when the indents are zero, and on a list item.
  // The list's shift tag does not set these pixels, so the gap shows. A
  // heading that is also a list item can still hide space before.
  int heading = 0;
  std::string font = "Sans";
  if (const Style* style = find_style(styles_, format.style)) {
    if (style->heading >= 1 && style->heading <= 6)
      heading = style->heading;
    if (!style->format.font.empty())
      font = style->format.font;
  }
  // A page break on a cell paragraph is kept in the file and does not open a
  // gap. The sheet break belongs to ordinary text.
  const bool page_break = format.page_break && format.cell.table == 0;
  apply_spacing_pixels(tag, format.spacing, format.basis, font, heading, page_break);
  apply_tab_stops(tag, format.tabs, format.indents, false, format.list.kind != ListKind::None,
                  zoom_factor());
}

Glib::RefPtr<Gtk::TextTag> MainWindow::para_tag_at(Gtk::TextIter iter) const
{
  ParaFormat ignored;
  for (const auto& tag : iter.get_tags()) {
    if (parse_para(tag_name(tag), ignored))
      return tag;
  }
  return {};
}

int MainWindow::paragraph_start(int offset) const
{
  // Paragraphs end at '\n' only, as in capture(). GTK's own lines also break
  // at other separators.
  auto iter = buffer_->get_iter_at_offset(offset);
  while (iter.get_offset() > 0) {
    auto prev = iter;
    prev.backward_char();
    if (prev.get_char() == '\n')
      break;
    iter = prev;
  }
  return iter.get_offset();
}

int MainWindow::paragraph_end(int offset) const
{
  // Just past the paragraph's newline, or the end of the buffer.
  auto iter = buffer_->get_iter_at_offset(offset);
  while (!iter.is_end()) {
    const bool newline = iter.get_char() == '\n';
    ++iter;
    if (newline)
      break;
  }
  return iter.get_offset();
}

bool MainWindow::final_paragraph_empty() const
{
  // A trailing page-break pad is not text. "Hello\n" plus the pad is still an
  // empty last paragraph; a real character before the pad is not.
  const int count = buffer_->get_char_count();
  if (count == 0)
    return true;
  auto iter = buffer_->get_iter_at_offset(count);
  while (iter.backward_char()) {
    const gunichar ch = iter.get_char();
    if (ch == kPageBreakPad)
      continue;
    return ch == '\n';
  }
  return true;
}

void MainWindow::drop_page_break_pads()
{
  // A pad beside a real character makes Backspace eat the pad first, and the
  // buffer then disagrees with capture(), which skips the pad. Once the
  // paragraph has text, the seam sits on that text.
  if (!buffer_ || dropping_pad_ || loading_ || restoring_)
    return;
  const int count = buffer_->get_char_count();
  std::vector<int> pads;
  for (int start = 0; start <= count;) {
    const int end = paragraph_end(start);
    bool real = false;
    std::vector<int> here;
    for (auto iter = buffer_->get_iter_at_offset(start); !iter.is_end() && iter.get_offset() < end;
         ++iter) {
      const gunichar ch = iter.get_char();
      if (ch == kPageBreakPad)
        here.push_back(iter.get_offset());
      else if (ch != '\n')
        real = true;
    }
    if (real)
      pads.insert(pads.end(), here.begin(), here.end());
    if (end <= start || end >= count)
      break;
    start = end;
  }
  if (pads.empty())
    return;
  std::sort(pads.begin(), pads.end(), [](int a, int b) { return a > b; });
  dropping_pad_ = true;
  for (int at : pads) {
    auto from = buffer_->get_iter_at_offset(at);
    auto to = from;
    to.forward_char();
    buffer_->erase(from, to);
  }
  dropping_pad_ = false;
}

ParaFormat MainWindow::para_at(int offset) const
{
  // The same rule as capture(): the paragraph's first tagged character, then
  // the pending format of an empty last paragraph, then the paragraph above.
  auto iter = buffer_->get_iter_at_offset(paragraph_start(offset));
  const auto line_start = iter;
  for (; !iter.is_end(); ++iter) {
    ParaFormat format;
    if (auto tag = para_tag_at(iter)) {
      if (parse_para(tag_name(tag), format))
        return format;
    }
    if (iter.get_char() == '\n')
      break;
  }
  if (iter.is_end() && pending_para_set_)
    return pending_para_;
  if (line_start.get_offset() > 0) {
    // The format above, without its page break. capture() keeps a break on
    // the paragraph that has it; an untagged line must not start a sheet.
    ParaFormat above = para_at(line_start.get_offset() - 1);
    above.page_break = false;
    return above;
  }
  return ParaFormat{};
}

int MainWindow::basis_half_at(int offset) const
{
  if (!buffer_)
    return 22;
  const int start = paragraph_start(offset);
  const int end = paragraph_end(offset);
  for (auto iter = buffer_->get_iter_at_offset(start); !iter.is_end() && iter.get_offset() < end;
       ++iter) {
    const gunichar ch = iter.get_char();
    if (ch == '\n' || ch == kPageBreakPad)
      continue;
    if (has_fmt(iter))
      return half_points_of(format_of(iter).size);
  }
  if (end > start) {
    auto nl = buffer_->get_iter_at_offset(end);
    if (nl.backward_char() && nl.get_char() == '\n' && has_fmt(nl))
      return half_points_of(format_of(nl).size);
  }
  const int tagged = para_at(offset).basis;
  return tagged >= 2 ? tagged : 22;
}

Indents MainWindow::indents_at(int offset) const
{
  return para_at(offset).indents;
}

ParaFormat MainWindow::destination_para(int start, int end) const
{
  // Text before the insertion on the same line, then text after it, decides.
  if (start > 0) {
    auto before = buffer_->get_iter_at_offset(start - 1);
    ParaFormat format;
    if (before.get_char() != '\n') {
      if (auto tag = para_tag_at(before)) {
        if (parse_para(tag_name(tag), format))
          return format;
      }
    }
  }
  for (auto after = buffer_->get_iter_at_offset(end); !after.is_end(); ++after) {
    ParaFormat format;
    if (auto tag = para_tag_at(after)) {
      if (parse_para(tag_name(tag), format))
        return format;
    }
    if (after.get_char() == '\n')
      break;
  }
  if (pending_para_set_)
    return pending_para_;
  if (start > 0)
    return para_at(start - 1);
  return ParaFormat{};
}

void MainWindow::note_touched(int from, int to)
{
  if (loading_ || restoring_ || normalising_ || !in_user_ || !buffer_)
    return;
  if (!touched_start_) {
    touched_start_ = buffer_->create_mark(buffer_->begin(), true);
    touched_end_ = buffer_->create_mark(buffer_->begin(), false);
  }
  if (!touched_) {
    touched_ = true;
    buffer_->move_mark(touched_start_, buffer_->get_iter_at_offset(from));
    buffer_->move_mark(touched_end_, buffer_->get_iter_at_offset(to));
    return;
  }
  if (from < touched_start_->get_iter().get_offset())
    buffer_->move_mark(touched_start_, buffer_->get_iter_at_offset(from));
  if (to > touched_end_->get_iter().get_offset())
    buffer_->move_mark(touched_end_, buffer_->get_iter_at_offset(to));
}

void MainWindow::normalise_paragraphs()
{
  // A deleted newline joins two paragraphs. The joined paragraph keeps the
  // first one's indents, alignment and list, as AbiWord and LibreOffice do.
  // Only the paragraphs the action touched can have come apart: the others
  // were whole after the last action, so typing costs a paragraph, not the
  // document.
  const int newline_at = inserted_newline_at_;
  inserted_newline_at_ = -1;
  if (!touched_)
    return;
  touched_ = false;
  const int count = buffer_->get_char_count();
  int begin = paragraph_start(touched_start_->get_iter().get_offset());
  const int last = paragraph_end(touched_end_->get_iter().get_offset());
  // The paragraph above is whole: its tag is the one an untagged paragraph
  // follows.
  Glib::RefPtr<Gtk::TextTag> carry;
  if (begin > 0)
    carry = para_tag_at(buffer_->get_iter_at_offset(paragraph_start(begin - 1)));
  if (!carry)
    carry = para_tag(ParaFormat{});
  // Enter splits one paragraph into two that share its tag. The page break
  // stays on the paragraph that already started the sheet: the text, when
  // the newline was inserted at its start, otherwise the paragraph that
  // contains the newline. The other half does not start another sheet.
  int keeper = -1;
  if (newline_at >= 0) {
    if (paragraph_start(newline_at) == newline_at)
      keeper = newline_at + 1;
    else
      keeper = paragraph_start(newline_at);
  }
  const bool keeper_break = keeper >= 0 && para_at(keeper).page_break;
  // Enter at the end of a sheet copies that break onto the new line. The
  // line's previous character is the sheet's own newline, so the landing
  // format still says page break, and the empty tail reports none: it is
  // not a paragraph that had the break. The new line does not start a sheet.
  // A break that lives only on the tail (no character of its own) is the
  // keeper, and the line in front of it is what loses the copy.
  const bool strip_new_end = newline_at >= 0 && newline_at + 1 >= count &&
                             paragraph_start(newline_at) == newline_at && !keeper_break;
  // Pending still names the paragraph that was empty, pad included. The tail
  // after a trailing newline is a new paragraph and must not take that break.
  // A break with no character stays pending: that tail is the sheet.
  if (newline_at >= 0 && newline_at + 1 >= count && pending_para_set_ &&
      paragraph_start(newline_at) != newline_at)
    pending_para_.page_break = false;
  normalising_ = true;
  while (begin < count && begin <= last) {
    Glib::RefPtr<Gtk::TextTag> chosen;
    std::vector<Glib::RefPtr<Gtk::TextTag>> seen;
    bool uniform = true;
    int end = begin;
    for (auto iter = buffer_->get_iter_at_offset(begin); !iter.is_end(); ++iter) {
      auto tag = para_tag_at(iter);
      if (tag && !chosen)
        chosen = tag;
      if (tag && std::find(seen.begin(), seen.end(), tag) == seen.end())
        seen.push_back(tag);
      if (!tag || tag != chosen)
        uniform = false;
      end = iter.get_offset() + 1;
      if (iter.get_char() == '\n')
        break;
    }
    if (!chosen) {
      // Enter copies the paragraph above. The page break stays on the one
      // that had it; the new paragraph does not take a second break.
      ParaFormat carried;
      if (parse_para(tag_name(carry), carried) && carried.page_break) {
        carried.page_break = false;
        chosen = para_tag(carried);
      } else {
        chosen = carry;
      }
      uniform = false;
    }
    if (chosen && ((keeper_break && begin != keeper) || (strip_new_end && begin == newline_at))) {
      ParaFormat format;
      if (parse_para(tag_name(chosen), format) && format.page_break) {
        format.page_break = false;
        chosen = para_tag(format);
        uniform = false;
      }
    }
    if (!uniform) {
      for (const auto& tag : seen)
        buffer_->remove_tag(tag, buffer_->get_iter_at_offset(begin),
                            buffer_->get_iter_at_offset(end));
      buffer_->apply_tag(chosen, buffer_->get_iter_at_offset(begin),
                         buffer_->get_iter_at_offset(end));
    }
    carry = chosen;
    begin = end;
  }
  normalising_ = false;
}

bool deleting_pad_clears_break(gunichar erased, bool only_char, bool page_break)
{
  return only_char && page_break && erased == kPageBreakPad;
}

void MainWindow::on_erase(const Gtk::TextBuffer::iterator& from,
                          const Gtk::TextBuffer::iterator& to)
{
  // A deleted picture leaves the buffer. The undo step has already copied
  // its bytes. Drop the map entry while the pixbuf is still alive. A replay
  // puts the entry back itself, so leave the map alone then.
  if (!loading_ && !restoring_ && from != to) {
    for (auto iter = from; iter.compare(to) < 0; ++iter) {
      if (auto pix = iter.get_pixbuf())
        image_pix_.erase(pix->gobj());
    }
  }
  // Erasing through to the end of the buffer from the start of a line leaves
  // an empty last paragraph. Like Word's surviving paragraph mark, it keeps
  // the format of the last paragraph that was there.
  if (loading_ || restoring_ || !buffer_ || !to.is_end() || from == to)
    return;
  if (paragraph_start(from.get_offset()) != from.get_offset())
    return;
  pending_para_ = para_at(buffer_->get_char_count());
  pending_para_set_ = true;
  // The seam sat on the pad. Delete removes that character, so the sheet is
  // gone; keeping the break would save a page the screen no longer shows.
  // Putting the pad back would make Delete look like it did nothing.
  bool only_char = false;
  if (to.get_offset() == from.get_offset() + 1) {
    only_char = true;
    const int end = paragraph_end(from.get_offset());
    for (auto iter = buffer_->get_iter_at_offset(from.get_offset()); iter.get_offset() < end;
         ++iter) {
      if (iter.get_offset() == from.get_offset() || iter.get_char() == '\n')
        continue;
      only_char = false;
      break;
    }
  }
  if (deleting_pad_clears_break(from.get_char(), only_char, pending_para_.page_break))
    pending_para_.page_break = false;
}

void MainWindow::end_typing_run()
{
  last_typed_us_ = 0;
  last_merge_kind_ = MergeKind::None;
  story_continue_view_ = nullptr;
  story_continue_caret_ = -1;
  merge_cell_caret_ = -1;
}

int MainWindow::heading_level_of(const ParaFormat& format) const
{
  if (const Style* style = find_style(styles_, format.style)) {
    if (style->heading >= 1 && style->heading <= 6)
      return style->heading;
  }
  return 0;
}

bool MainWindow::needs_spacing_gap(const ParaFormat& format, int heading) const
{
  if (format.list.kind == ListKind::None || format.spacing == Spacing{})
    return false;
  if (heading >= 1 && heading <= 6)
    return true;
  return heading_level_of(format) > 0;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::spacing_gap_tag(const ParaFormat& format, int heading)
{
  const int level = std::max(heading_level_of(format), heading >= 1 && heading <= 6 ? heading : 0);
  const Glib::ustring name =
      std::string("gap") + '\x1f' + std::to_string(level) + '\x1f' + para_name(format);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (tag)
    return tag;
  tag = Gtk::TextTag::create(name);
  std::string font = "Sans";
  if (const Style* style = find_style(styles_, format.style)) {
    if (!style->format.font.empty())
      font = style->format.font;
  }
  apply_spacing_pixels(tag, format.spacing, format.basis, font, level, false);
  table->add(tag);
  if (!loading_)
    raise_headings();
  return tag;
}

void MainWindow::apply_spacing_gap(int start, int end, const ParaFormat& format, int heading)
{
  if (!buffer_ || start >= end)
    return;
  std::vector<Glib::RefPtr<Gtk::TextTag>> old;
  for (auto iter = buffer_->get_iter_at_offset(start); iter.get_offset() < end; ++iter) {
    for (const auto& tag : iter.get_tags()) {
      if (!is_spacing_gap(tag_name(tag)))
        continue;
      if (std::find(old.begin(), old.end(), tag) == old.end())
        old.push_back(tag);
    }
  }
  for (const auto& tag : old)
    buffer_->remove_tag(tag, buffer_->get_iter_at_offset(start), buffer_->get_iter_at_offset(end));
  if (!needs_spacing_gap(format, heading))
    return;
  buffer_->apply_tag(spacing_gap_tag(format, heading), buffer_->get_iter_at_offset(start),
                     buffer_->get_iter_at_offset(end));
}

Gtk::TextView* MainWindow::focused_story_view() const
{
  if (story_target_ == 1)
    return const_cast<Gtk::TextView*>(&header_view_);
  if (story_target_ == 2)
    return const_cast<Gtk::TextView*>(&footer_view_);
  if (story_target_ == 3) {
    if (story_focus_view_ != nullptr && story_focus_view_->get_buffer())
      return story_focus_view_;
    return const_cast<PageText*>(&notes_view_);
  }
  return nullptr;
}

bool MainWindow::note_at_view(Gtk::TextView& view, int caret, int& note_index,
                              size_t& para_index) const
{
  std::vector<int> indexes;
  if (note_groups_.size() <= 1) {
    indexes.resize(notes_.size());
    for (size_t i = 0; i < notes_.size(); ++i)
      indexes[i] = static_cast<int>(i);
  } else if (&view == &notes_view_) {
    indexes = note_groups_.front().indexes;
  } else {
    for (size_t i = 1; i < note_groups_.size() && i - 1 < note_bands_.size(); ++i) {
      if (note_bands_[i - 1] && &note_bands_[i - 1]->view == &view) {
        indexes = note_groups_[i].indexes;
        break;
      }
    }
  }
  int last_note = -1;
  size_t last_para = 0;
  int offset = 0;
  for (size_t n = 0; n < indexes.size(); ++n) {
    const int note = indexes[n];
    if (note < 0 || note >= static_cast<int>(notes_.size()))
      continue;
    if (n)
      offset += 2;
    const std::vector<Paragraph>& paras = notes_[static_cast<size_t>(note)];
    for (size_t p = 0; p < paras.size(); ++p) {
      const int len = static_cast<int>(Glib::ustring(paragraph_plain(paras[p])).length());
      last_note = note;
      last_para = p;
      if (caret <= offset + len) {
        note_index = note;
        para_index = p;
        return true;
      }
      offset += len;
      if (p + 1 < paras.size())
        offset += 1;
    }
  }
  if (last_note < 0)
    return false;
  note_index = last_note;
  para_index = last_para;
  return true;
}

ParaFormat MainWindow::focused_para() const
{
  if (editing_cell()) {
    if (TableSheet* sheet = editing_sheet()) {
      int row = 0;
      int column = 0;
      int caret = 0;
      int bound = 0;
      if (cell_place(*sheet, row, column, caret, bound)) {
        if (const Paragraph* form = cell_paragraph_at(row, column, caret))
          return para_format(*form);
      }
    }
  }
  if (Gtk::TextView* view = focused_story_view()) {
    if (story_target_ == 1 || story_target_ == 2) {
      const std::vector<Paragraph>& story = story_target_ == 1 ? header_ : footer_;
      if (view->get_buffer() && !story.empty()) {
        const int caret = view->get_buffer()->get_insert()->get_iter().get_offset();
        int offset = 0;
        size_t index = 0;
        for (; index < story.size(); ++index) {
          const int len = static_cast<int>(Glib::ustring(paragraph_plain(story[index])).length());
          if (caret <= offset + len)
            break;
          offset += len + 1;
        }
        if (index >= story.size())
          index = story.size() - 1;
        return para_format(story[index]);
      }
    } else if (story_target_ == 3 && view->get_buffer()) {
      int note_index = -1;
      size_t para_index = 0;
      const int caret = view->get_buffer()->get_insert()->get_iter().get_offset();
      if (note_at_view(*view, caret, note_index, para_index))
        return para_format(notes_[static_cast<size_t>(note_index)][para_index]);
    }
  }
  return para_at(cursor_offset());
}

void MainWindow::apply_para_edit(const std::function<void(ParaFormat&)>& edit)
{
  // Each paragraph is edited from its own format, so changing one property
  // keeps the others where the paragraphs differ.
  apply_paragraphs([this, &edit](std::vector<Paragraph>& paragraphs) {
    for (Paragraph& paragraph : paragraphs) {
      ParaFormat format = para_format(paragraph);
      edit(format);
      // What the edit changed is the paragraph's own now, as is an
      // alignment chosen with the buttons or menu, changed or not.
      if (format.indents.left != paragraph.indents.left)
        format.direct |= kDirectLeft;
      if (format.indents.right != paragraph.indents.right)
        format.direct |= kDirectRight;
      if (format.indents.first != paragraph.indents.first)
        format.direct |= kDirectFirst;
      if (format.align != paragraph.align || chose_align_)
        format.direct |= kDirectAlign;
      if (format.spacing.before != paragraph.spacing.before)
        format.direct |= kDirectBefore;
      if (format.spacing.after != paragraph.spacing.after)
        format.direct |= kDirectAfter;
      if (!same_spacing_line(format.spacing, paragraph.spacing))
        format.direct |= kDirectLine;
      if (!(format.borders == paragraph.borders))
        format.direct |= kDirectBorders;
      if (!(format.shading == paragraph.shading))
        format.direct |= kDirectShading;
      if (format.tabs != paragraph.tabs)
        format.direct |= kDirectTabs;
      const Style* styled = find_style(styles_, format.style);
      const std::vector<TabStop> from_style =
          styled ? clamp_tabs(styled->tabs) : std::vector<TabStop>{};
      if (clamp_tabs(format.tabs) == from_style) {
        format.direct &= ~static_cast<unsigned>(kDirectTabs);
        format.tabs = from_style;
      }
      paragraph.indents = format.indents;
      paragraph.align = format.align;
      paragraph.list = format.list;
      paragraph.spacing = format.spacing;
      paragraph.basis = format.basis;
      paragraph.borders = format.borders;
      paragraph.shading = format.shading;
      paragraph.tabs = format.tabs;
      paragraph.direct = format.direct;
    }
  });
}

void MainWindow::apply_paragraphs(const std::function<void(std::vector<Paragraph>&)>& edit)
{
  // A dialog or a toolbar button takes the keyboard. The cell, or the story
  // that had it, is still the paragraph the command is about.
  if (editing_cell()) {
    apply_cell_paragraphs(edit);
    return;
  }
  if (story_target_ == 1 || story_target_ == 2) {
    std::vector<Paragraph>& story = story_target_ == 1 ? header_ : footer_;
    Gtk::TextView& view = story_target_ == 1 ? header_view_ : footer_view_;
    if (!view.get_buffer() || story.empty())
      return;
    auto buffer = view.get_buffer();
    int from = buffer->get_insert()->get_iter().get_offset();
    int to = from;
    Gtk::TextBuffer::iterator bound_a;
    Gtk::TextBuffer::iterator bound_b;
    if (buffer->get_selection_bounds(bound_a, bound_b)) {
      from = bound_a.get_offset();
      to = bound_b.get_offset();
    }
    std::vector<int> lengths;
    lengths.reserve(story.size());
    for (const Paragraph& paragraph : story)
      lengths.push_back(static_cast<int>(Glib::ustring(paragraph_plain(paragraph)).length()));
    std::vector<int> leading(lengths.size(), 1);
    if (!leading.empty())
      leading[0] = 0;
    const std::vector<size_t> indexes = story_selection(lengths, leading, from, to);
    if (indexes.empty())
      return;
    const Document before_doc = capture();
    Document after = before_doc;
    std::vector<Paragraph>& target = story_target_ == 1 ? after.header : after.footer;
    std::vector<Paragraph> edited;
    for (size_t index : indexes) {
      if (index >= target.size())
        return;
      edited.push_back(target[index]);
    }
    edit(edited);
    for (size_t i = 0; i < indexes.size(); ++i)
      target[indexes[i]] = edited[i];
    if (before_doc == after)
      return;
    commit_document(before_doc, after);
    view.grab_focus();
    return;
  }
  if (story_target_ == 3) {
    Gtk::TextView* view = focused_story_view();
    if (!view || !view->get_buffer())
      return;
    auto buffer = view->get_buffer();
    int from = buffer->get_insert()->get_iter().get_offset();
    int to = from;
    Gtk::TextBuffer::iterator bound_a;
    Gtk::TextBuffer::iterator bound_b;
    if (buffer->get_selection_bounds(bound_a, bound_b)) {
      from = bound_a.get_offset();
      to = bound_b.get_offset();
    }
    std::vector<int> note_indexes;
    if (note_groups_.size() <= 1) {
      note_indexes.resize(notes_.size());
      for (size_t i = 0; i < notes_.size(); ++i)
        note_indexes[i] = static_cast<int>(i);
    } else if (view == &notes_view_) {
      note_indexes = note_groups_.front().indexes;
    } else {
      for (size_t i = 1; i < note_groups_.size() && i - 1 < note_bands_.size(); ++i) {
        if (note_bands_[i - 1] && &note_bands_[i - 1]->view == view) {
          note_indexes = note_groups_[i].indexes;
          break;
        }
      }
    }
    struct Place {
      int note = -1;
      size_t para = 0;
    };
    std::vector<int> lengths;
    std::vector<int> leading;
    std::vector<Place> places;
    for (size_t n = 0; n < note_indexes.size(); ++n) {
      const int note = note_indexes[n];
      if (note < 0 || note >= static_cast<int>(notes_.size()))
        continue;
      const std::vector<Paragraph>& paras = notes_[static_cast<size_t>(note)];
      for (size_t p = 0; p < paras.size(); ++p) {
        int gap = 1;
        if (lengths.empty())
          gap = 0;
        else if (p == 0)
          gap = 2;
        lengths.push_back(static_cast<int>(Glib::ustring(paragraph_plain(paras[p])).length()));
        leading.push_back(gap);
        places.push_back(Place{note, p});
      }
    }
    const std::vector<size_t> picked = story_selection(lengths, leading, from, to);
    if (picked.empty())
      return;
    const Document before_doc = capture();
    Document after = before_doc;
    std::vector<Paragraph> edited;
    for (size_t index : picked) {
      const Place& place = places[index];
      if (place.note < 0 || static_cast<size_t>(place.note) >= after.notes.size() ||
          place.para >= after.notes[static_cast<size_t>(place.note)].size())
        return;
      edited.push_back(after.notes[static_cast<size_t>(place.note)][place.para]);
    }
    edit(edited);
    for (size_t i = 0; i < picked.size(); ++i) {
      const Place& place = places[picked[i]];
      after.notes[static_cast<size_t>(place.note)][place.para] = edited[i];
    }
    if (!(before_doc == after)) {
      commit_document(before_doc, after);
      view->grab_focus();
    }
    return;
  }
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  buffer_->get_selection_bounds(start_iter, end_iter);
  const int sel_start = start_iter.get_offset();
  const int sel_end = end_iter.get_offset();
  const int count = buffer_->get_char_count();
  // Every paragraph the selection touches. A selection that ends just after
  // a newline does not reach the next one.
  const int last = paragraph_start(sel_end > sel_start ? sel_end - 1 : sel_end);
  std::vector<int> starts;
  for (int at = paragraph_start(sel_start);;) {
    starts.push_back(at);
    if (at >= last)
      break;
    at = paragraph_end(at);
  }
  // The edit sees each paragraph's format, not its text.
  std::vector<Paragraph> paragraphs;
  for (int at : starts) {
    const ParaFormat format = para_at(at);
    Paragraph paragraph;
    paragraph.indents = format.indents;
    paragraph.align = format.align;
    paragraph.list = format.list;
    paragraph.style = format.style;
    paragraph.direct = format.direct;
    paragraph.page_break = format.page_break;
    paragraph.cell = format.cell;
    paragraph.spacing = format.spacing;
    paragraph.borders = format.borders;
    paragraph.shading = format.shading;
    paragraph.tabs = format.tabs;
    // No runs here. The line's size comes from the buffer, or a 20 pt
    // paragraph would be drawn as 11 pt after OK.
    paragraph.basis = basis_half_at(at);
    paragraphs.push_back(paragraph);
  }
  edit(paragraphs);
  buffer_->begin_user_action();
  // An empty last paragraph left out of the selection keeps what it had,
  // rather than following the paragraph above into the new format.
  if (last < count && final_paragraph_empty() && !pending_para_set_) {
    pending_para_ = para_at(count);
    pending_para_set_ = true;
  }
  for (size_t i = 0; i < starts.size(); ++i)
    tag_paragraph(starts[i], para_format(paragraphs[i]));
  buffer_->end_user_action();
  buffer_->select_range(buffer_->get_iter_at_offset(sel_start),
                        buffer_->get_iter_at_offset(sel_end));
  ruler_.queue_draw();
  text_.queue_draw();
  show_align();
  sync_list_controls();
  text_.grab_focus();
}

void MainWindow::tag_paragraph(int start, const ParaFormat& format)
{
  const int end = paragraph_end(start);
  if (start == end) {
    // The empty last paragraph has nothing to tag. Hold its format aside.
    pending_para_ = format;
    pending_para_.indents = clamp_indents(format.indents);
    pending_para_.list = clamp_list(format.list);
    pending_para_.spacing = clamp_spacing(format.spacing);
    pending_para_.borders = clamp_borders(format.borders);
    pending_para_.shading = clamp_shading(format.shading);
    pending_para_.tabs = clamp_tabs(format.tabs);
    pending_para_.cell.borders = clamp_borders(format.cell.borders);
    pending_para_.cell.shading = clamp_shading(format.cell.shading);
    if (pending_para_.basis < 2)
      pending_para_.basis = 22;
    pending_para_set_ = true;
    return;
  }
  std::vector<Glib::RefPtr<Gtk::TextTag>> seen;
  for (auto iter = buffer_->get_iter_at_offset(start); iter.get_offset() < end; ++iter) {
    auto tag = para_tag_at(iter);
    if (tag && std::find(seen.begin(), seen.end(), tag) == seen.end())
      seen.push_back(tag);
  }
  for (const auto& tag : seen)
    buffer_->remove_tag(tag, buffer_->get_iter_at_offset(start), buffer_->get_iter_at_offset(end));
  buffer_->apply_tag(para_tag(format), buffer_->get_iter_at_offset(start),
                     buffer_->get_iter_at_offset(end));
  int heading = heading_level_of(format);
  apply_spacing_gap(start, end, format, heading);
}

size_t MainWindow::caret_paragraph() const
{
  return document_index_at(cursor_offset());
}

bool MainWindow::renumber_list(bool restart)
{
  if (!buffer_)
    return false;
  const Document doc = capture();
  const size_t index = caret_paragraph();
  std::vector<Paragraph> paragraphs = doc.paragraphs;
  const bool changed =
      restart ? restart_numbering(paragraphs, index) : continue_numbering(paragraphs, index);
  if (!changed || paragraphs.size() != doc.paragraphs.size())
    return false;
  // Only the list labels change; every paragraph whose label did is retagged.
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  buffer_->get_selection_bounds(start_iter, end_iter);
  const int sel_start = start_iter.get_offset();
  const int sel_end = end_iter.get_offset();
  buffer_->begin_user_action();
  for (const ListRetag& tag : list_retag_offsets(doc.paragraphs, paragraphs))
    tag_paragraph(tag.offset, para_format(paragraphs[tag.index]));
  buffer_->end_user_action();
  buffer_->select_range(buffer_->get_iter_at_offset(sel_start),
                        buffer_->get_iter_at_offset(sel_end));
  text_.queue_draw();
  text_.grab_focus();
  return true;
}

void MainWindow::sync_context_numbering()
{
  if (!context_restart_ || !context_continue_ || !context_numbering_rule_)
    return;
  bool numbered = false;
  bool can_restart = false;
  bool can_continue = false;
  if (buffer_) {
    const Document doc = capture();
    const size_t index = caret_paragraph();
    if (index < doc.paragraphs.size() &&
        clamp_list(doc.paragraphs[index].list).kind == ListKind::Number) {
      numbered = true;
      std::vector<Paragraph> trial = doc.paragraphs;
      can_restart = restart_numbering(trial, index);
      trial = doc.paragraphs;
      can_continue = continue_numbering(trial, index);
    }
  }
  context_numbering_rule_->set_visible(numbered);
  context_restart_->set_visible(numbered);
  context_continue_->set_visible(numbered);
  context_restart_->set_sensitive(can_restart);
  context_continue_->set_sensitive(can_continue);
}

void MainWindow::apply_align(Align align)
{
  chose_align_ = true;
  apply_para_edit([align](ParaFormat& format) { format.align = align; });
  chose_align_ = false;
}

void MainWindow::on_align_toggled(Align align)
{
  if (suppress_format_)
    return;
  // The four buttons act as one group, as in Word 97. Pressing the button
  // already down leaves the paragraph as it is, except Center, Align Right
  // and Justify, which go back to left.
  Gtk::ToggleToolButton* button = align == Align::Center    ? align_center_toggle_
                                  : align == Align::Right   ? align_right_toggle_
                                  : align == Align::Justify ? justify_toggle_
                                                            : align_left_toggle_;
  if (button && !button->get_active())
    align = Align::Left;
  apply_align(align);
}

void MainWindow::show_align()
{
  if (!buffer_)
    return;
  const Align align = focused_para().align;
  const bool guard = suppress_format_;
  suppress_format_ = true;
  auto show = [](Gtk::ToggleToolButton* button, bool on) {
    if (button && button->get_active() != on)
      button->set_active(on);
  };
  show(align_left_toggle_, align == Align::Left);
  show(align_center_toggle_, align == Align::Center);
  show(align_right_toggle_, align == Align::Right);
  show(justify_toggle_, align == Align::Justify);
  suppress_format_ = guard;
}

void MainWindow::toggle_list_kind(ListKind kind)
{
  // The toolbar toggles fire when sync_list_controls() sets them, too.
  if (suppress_format_ || !buffer_)
    return;
  apply_paragraphs([kind](std::vector<Paragraph>& paragraphs) { toggle_list(paragraphs, kind); });
}

bool MainWindow::shift_list_level(int delta)
{
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  buffer_->get_selection_bounds(start_iter, end_iter);
  bool any = false;
  for (int at = paragraph_start(start_iter.get_offset());;) {
    if (para_at(at).list.kind != ListKind::None)
      any = true;
    const int next = paragraph_end(at);
    if (any || next >= end_iter.get_offset() || next == at)
      break;
    at = next;
  }
  if (!any)
    return false;
  apply_paragraphs([delta](std::vector<Paragraph>& paragraphs) {
    for (Paragraph& paragraph : paragraphs)
      set_list_level(paragraph, clamp_list(paragraph.list).level + delta);
  });
  return true;
}

bool MainWindow::on_text_key(GdkEventKey* event)
{
  // Typing in the body leaves the cell a menu or the style list had focused
  // away from. The key still does what it would in the body.
  leave_cell_edit();
  // Word's list keys. Tab and Shift+Tab at the start of a list item, or over
  // several paragraphs, move it down or up a level. Enter on an empty item
  // and Backspace at the start of one end the list there.
  if (!buffer_ || event->type != GDK_KEY_PRESS)
    return false;
  const guint mods = event->state & gtk_accelerator_get_default_mod_mask();
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  const bool selection = buffer_->get_selection_bounds(start_iter, end_iter);
  const int caret = cursor_offset();
  const bool at_start = !selection && paragraph_start(caret) == caret;
  const bool in_list = para_at(caret).list.kind != ListKind::None;
  switch (event->keyval) {
    case GDK_KEY_Tab:
    case GDK_KEY_KP_Tab:
    case GDK_KEY_ISO_Left_Tab: {
      if ((mods & ~static_cast<guint>(GDK_SHIFT_MASK)) != 0)
        return false;
      const bool across = selection && paragraph_start(start_iter.get_offset()) !=
                                           paragraph_start(end_iter.get_offset());
      if (!(at_start && in_list) && !across)
        return false;
      const bool up = (mods & GDK_SHIFT_MASK) != 0 || event->keyval == GDK_KEY_ISO_Left_Tab;
      return shift_list_level(up ? -1 : 1);
    }
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter: {
      if (mods == 0 && !selection && !(at_start && in_list)) {
        // Enter at the end of a paragraph: the new one takes the style's
        // next style, once GTK has made it (on_user_end).
        const auto here = buffer_->get_iter_at_offset(caret);
        if (here.is_end() || here.get_char() == '\n') {
          next_style_pending_ = true;
          next_style_from_ = paragraph_index(caret);
        }
        return false;
      }
      if (mods != 0 || !at_start || !in_list)
        return false;
      // The caret sits on a page-break pad in an otherwise empty item. That
      // pad is not text, so the item is blank and Enter ends the list.
      const int from = paragraph_start(caret);
      const int to = paragraph_end(caret);
      const Glib::ustring item = buffer_->get_slice(buffer_->get_iter_at_offset(from),
                                                    buffer_->get_iter_at_offset(to), true);
      if (!paragraph_is_blank_item(item))
        return false;
      toggle_list_kind(para_at(caret).list.kind);
      return true;
    }
    case GDK_KEY_BackSpace:
      if (mods != 0 || !at_start || !in_list)
        return false;
      toggle_list_kind(para_at(caret).list.kind);
      return true;
    default:
      return false;
  }
}

std::string list_label_key(const std::string& text, const std::string& family, int size, bool bold,
                           bool italic, bool underline, int color, int highlight, bool strike,
                           int script)
{
  std::string key = text;
  key += '\n';
  key += family;
  key += '\n';
  key += std::to_string(size);
  key += bold ? "b" : "";
  key += italic ? "i" : "";
  key += underline ? "u" : "";
  key += '\n';
  key += std::to_string(color);
  key += '\n';
  key += std::to_string(highlight);
  key += strike ? "s" : "";
  if (script == static_cast<int>(Script::Super))
    key += "u";
  else if (script == static_cast<int>(Script::Sub))
    key += "d";
  return key;
}

bool label_underlined(const Run& format)
{
  return format.underline;
}

void apply_label_underline(const Glib::RefPtr<Pango::Layout>& layout, bool underline)
{
  if (!layout || !underline)
    return;
  Pango::AttrInt mark = Pango::Attribute::create_attr_underline(Pango::UNDERLINE_SINGLE);
  mark.set_start_index(0);
  mark.set_end_index(G_MAXUINT);
  Pango::AttrList attrs;
  attrs.insert(mark);
  layout->set_attributes(attrs);
}

void apply_label_face(const Glib::RefPtr<Pango::Layout>& layout, const Run& format, int font_size)
{
  if (!layout)
    return;
  Pango::AttrList attrs;
  bool any = false;
  auto span = [](Pango::Attribute& attr) {
    attr.set_start_index(0);
    attr.set_end_index(G_MAXUINT);
  };
  if (format.underline) {
    Pango::AttrInt mark = Pango::Attribute::create_attr_underline(Pango::UNDERLINE_SINGLE);
    span(mark);
    attrs.insert(mark);
    any = true;
  }
  auto channel = [](int value) {
    return static_cast<guint16>(std::max(0, std::min(255, value)) * 65535 / 255);
  };
  if (format.color != Ink::Auto) {
    const Rgb c = ink_rgb(format.color);
    Pango::AttrColor fg =
        Pango::Attribute::create_attr_foreground(channel(c.red), channel(c.green), channel(c.blue));
    span(fg);
    attrs.insert(fg);
    any = true;
  }
  if (format.highlight != Ink::Auto) {
    const Rgb c = ink_rgb(format.highlight);
    Pango::AttrColor bg =
        Pango::Attribute::create_attr_background(channel(c.red), channel(c.green), channel(c.blue));
    span(bg);
    attrs.insert(bg);
    any = true;
  }
  if (format.strike) {
    Pango::AttrInt mark = Pango::Attribute::create_attr_strikethrough(true);
    span(mark);
    attrs.insert(mark);
    any = true;
  }
  if (format.note == 0 && format.script != Script::None) {
    Pango::AttrFloat scale = Pango::Attribute::create_attr_scale(0.65);
    span(scale);
    attrs.insert(scale);
    const int rise = static_cast<int>(font_size * 0.45);
    Pango::AttrInt up =
        Pango::Attribute::create_attr_rise(format.script == Script::Sub ? -rise : rise);
    span(up);
    attrs.insert(up);
    any = true;
  }
  if (any)
    layout->set_attributes(attrs);
}

Glib::RefPtr<Pango::Layout> MainWindow::list_label_layout(const Paragraph& paragraph, int offset,
                                                          int number, int& width, int& gap)
{
  const ListFormat list = clamp_list(paragraph.list);
  const auto iter = buffer_->get_iter_at_offset(offset);
  const Run format = !paragraph.runs.empty() ? paragraph.runs.front()
                     : paragraph.mark        ? *paragraph.mark
                                             : format_of(iter);
  const Glib::ustring text = list_label(list, number);
  const std::string family = format.font.empty() ? "Sans" : format.font;
  const int size = static_cast<int>(std::max(1.0, format.size) * zoom_factor() * PANGO_SCALE);
  const bool underline = label_underlined(format);
  const std::string key =
      list_label_key(text.raw(), family, size, format.bold, format.italic, underline,
                     static_cast<int>(format.color), static_cast<int>(format.highlight),
                     format.strike, static_cast<int>(format.script));
  const auto found = label_layout_cache_.find(key);
  if (found != label_layout_cache_.end()) {
    width = found->second.width;
    gap = found->second.gap;
    return found->second.layout;
  }
  ++label_layouts_;
  auto layout = text_.create_pango_layout(text);
  Pango::FontDescription desc;
  desc.set_family(family);
  desc.set_size(size);
  // Bold, italic, and underline, as Word 97 formats a label from the paragraph mark.
  desc.set_weight(format.bold ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL);
  desc.set_style(format.italic ? Pango::STYLE_ITALIC : Pango::STYLE_NORMAL);
  layout->set_font_description(desc);
  apply_label_face(layout, format, size);
  int height = 0;
  layout->get_pixel_size(width, height);
  auto space = text_.create_pango_layout(" ");
  space->set_font_description(desc);
  space->get_pixel_size(gap, height);
  // A document has few distinct labels; a hostile one cannot grow this far.
  if (label_layout_cache_.size() >= 4096)
    label_layout_cache_.clear();
  label_layout_cache_[key] = LabelLayout{layout, width, gap};
  return layout;
}

bool MainWindow::list_label_place(const Paragraph& paragraph, int offset, int number,
                                  Glib::RefPtr<Pango::Layout>& layout, int& x,
                                  Gdk::Rectangle& where)
{
  if (clamp_list(paragraph.list).kind == ListKind::None || !buffer_)
    return false;
  ++label_locates_;
  text_.get_iter_location(buffer_->get_iter_at_offset(offset), where);
  return list_label_at(paragraph, offset, number, layout, x, where);
}

bool MainWindow::list_label_at(const Paragraph& paragraph, int offset, int number,
                               Glib::RefPtr<Pango::Layout>& layout, int& x,
                               const Gdk::Rectangle& where)
{
  if (clamp_list(paragraph.list).kind == ListKind::None || !buffer_)
    return false;
  int width = 0;
  int gap = 0;
  layout = list_label_layout(paragraph, offset, number, width, gap);
  // Buffer x 0 is the page's left edge, as for the tag margins.
  const Indents indents = clamp_indents(paragraph.indents);
  x = list_label_x(paragraph.align, margin_left() + indent_px(indents.left + indents.first),
                   where.get_x(), width, indent_px(list_label_space(indents)), gap);
  return true;
}

void MainWindow::label_where(const Gtk::TextIter& start, int line_y, int line_height,
                             Gdk::Rectangle& where)
{
  const int line = start.get_line();
  const auto found = label_geometry_.find(line);
  if (found != label_geometry_.end() && found->second.line_height == line_height) {
    const LabelGeometry& g = found->second;
    where = Gdk::Rectangle(g.x, line_y + g.dy, g.width, g.height);
    return;
  }
  ++label_locates_;
  text_.get_iter_location(start, where);
  label_geometry_[line] = LabelGeometry{where.get_x(), where.get_y() - line_y, where.get_width(),
                                        where.get_height(), line_height};
}

void MainWindow::forget_label_geometry(int first, int last, int shift)
{
  if (label_geometry_.empty())
    return;
  label_geometry_.erase(label_geometry_.lower_bound(first), label_geometry_.upper_bound(last));
  if (shift == 0)
    return;
  std::map<int, LabelGeometry> moved;
  for (auto it = label_geometry_.begin(); it != label_geometry_.end(); ++it)
    moved.emplace_hint(moved.end(), it->first > last ? it->first + shift : it->first, it->second);
  label_geometry_.swap(moved);
}

void MainWindow::queue_list_shifts()
{
  if (list_shifts_queued_ || !buffer_)
    return;
  list_shifts_queued_ = true;
  // Before GTK lays the text out again and redraws it, so a centred item
  // never shows off centre in between.
  list_shifts_idle_ = Glib::signal_idle().connect(
      [this] {
        update_list_shifts();
        return false;
      },
      Glib::PRIORITY_HIGH_IDLE + 10);
}

Glib::RefPtr<Gtk::TextTag> MainWindow::list_shift_tag(int left_margin)
{
  const Glib::ustring name = kListShiftPrefix + std::to_string(left_margin);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    tag->property_left_margin() = left_margin;
    table->add(tag);
  }
  return tag;
}

void MainWindow::update_list_shifts()
{
  ++list_updates_;
  list_shifts_queued_ = false;
  if (!buffer_)
    return;
  auto table = buffer_->get_tag_table();
  std::vector<Glib::RefPtr<Gtk::TextTag>> old;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (is_list_shift(tag_name(tag)))
      old.push_back(tag);
  });
  if (old.empty() && !any_list())
    return;
  const std::vector<ListLine>& lines = list_lines();
  if (!list_lines_centred_ && old.empty())
    return;
  const int total = buffer_->get_char_count();
  // Does [s, e) hold `tag` anywhere, or all through?
  auto touches = [](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& s,
                    const Gtk::TextIter& e) {
    if (s.has_tag(tag))
      return true;
    auto it = s;
    return it.forward_to_tag_toggle(tag) && it.compare(e) < 0;
  };
  auto covers = [](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& s,
                   const Gtk::TextIter& e) {
    if (!s.has_tag(tag))
      return false;
    auto it = s;
    return !(it.forward_to_tag_toggle(tag) && it.compare(e) < 0);
  };
  shifting_ = true;
  std::vector<Glib::RefPtr<Gtk::TextTag>> used;
  // Each paragraph from its start to the next one's.
  int offset = 0;
  for (size_t i = 0; i < lines.size() && offset < total; ++i) {
    const ParaFormat& format = lines[i].format;
    const int end = i + 1 < lines.size()
                        ? std::min(total, buffer_->get_iter_at_line(lines[i + 1].line).get_offset())
                        : total;
    Glib::RefPtr<Gtk::TextTag> want;
    if (format.align == Align::Center && clamp_list(format.list).kind != ListKind::None) {
      const Indents indents = clamp_indents(format.indents);
      int width = 0;
      int gap = 0;
      list_label_layout(label_paragraph(i, offset), offset, lines[i].number, width, gap);
      // As list_label_place() measures: the first-line indent and the room.
      const int from =
          list_centre_from(format.align, margin_left() + indent_px(indents.left + indents.first),
                           width, indent_px(list_label_space(indents)), gap);
      // The paragraph tag's own left margin already centres it when equal.
      if (from >= 0 && from != std::max(0, margin_left() + indent_px(indents.left)))
        want = list_shift_tag(from);
    }
    const auto s = buffer_->get_iter_at_offset(offset);
    const auto e = buffer_->get_iter_at_offset(end);
    for (const auto& tag : old)
      if (tag != want && touches(tag, s, e))
        buffer_->remove_tag(tag, s, e);
    if (want) {
      if (!covers(want, s, e))
        buffer_->apply_tag(want, buffer_->get_iter_at_offset(offset),
                           buffer_->get_iter_at_offset(end));
      if (std::find(used.begin(), used.end(), want) == used.end())
        used.push_back(want);
    }
    offset = end;
  }
  // Tags no paragraph needs go, so the table holds one per offset in use.
  for (const auto& tag : old)
    if (std::find(used.begin(), used.end(), tag) == used.end())
      table->remove(tag);
  // A shift outranks every paragraph tag's indent. Raising a tag relays out
  // the text, so only when a paragraph tag sits above a shift.
  int top_para = -1;
  ParaFormat ignored;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (parse_para(tag_name(tag), ignored))
      top_para = std::max(top_para, tag->get_priority());
  });
  const bool low = std::any_of(
      used.begin(), used.end(),
      [&](const Glib::RefPtr<Gtk::TextTag>& tag) { return tag->get_priority() < top_para; });
  if (low) {
    for (const auto& tag : used)
      tag->set_priority(table->get_size() - 1);
    raise_headings();
  }
  shifting_ = false;
}

void MainWindow::queue_list_tabs()
{
  if (list_tabs_idle_.connected() || !buffer_)
    return;
  // Before GTK lays the text out again and redraws it.
  list_tabs_idle_ = Glib::signal_idle().connect(
      [this] {
        update_list_tabs();
        return false;
      },
      Glib::PRIORITY_HIGH_IDLE + 10);
}

Glib::RefPtr<Gtk::TextTag> MainWindow::list_tab_tag(int indent)
{
  const Glib::ustring name = kListTabPrefix + std::to_string(indent);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    tag->property_indent() = indent;
    table->add(tag);
  }
  return tag;
}

void MainWindow::note_list_tabs(const Gtk::TextIter& start, const Gtk::TextIter& end)
{
  if (!buffer_)
    return;
  if (!tabs_from_) {
    tabs_from_ = buffer_->create_mark(buffer_->begin(), true);
    tabs_to_ = buffer_->create_mark(buffer_->begin(), false);
  }
  if (!tabs_noted_ || start.compare(tabs_from_->get_iter()) < 0)
    buffer_->move_mark(tabs_from_, start);
  if (!tabs_noted_ || end.compare(tabs_to_->get_iter()) > 0)
    buffer_->move_mark(tabs_to_, end);
  tabs_noted_ = true;
}

void MainWindow::update_list_tabs()
{
  ++list_updates_;
  if (!buffer_)
    return;
  auto table = buffer_->get_tag_table();
  std::vector<Glib::RefPtr<Gtk::TextTag>> old;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (is_list_tab(tag_name(tag)))
      old.push_back(tag);
  });
  const int total = buffer_->get_char_count();
  const int lines = buffer_->get_line_count();
  // The paragraphs to look at again, as indices into tab_lines_.
  std::vector<char> dirty;
  std::vector<int> starts;
  const bool walk = tabs_full_ || tabs_renumber_ || static_cast<int>(tab_lines_.size()) != lines;
  if (walk) {
    // Each paragraph's start and format, as capture() reads them, and the
    // list numbers they give; GTK's lines also break at other separators.
    std::vector<Paragraph> paragraphs;
    auto it = buffer_->begin();
    starts.push_back(0);
    while (it.forward_line()) {
      auto before = it;
      before.backward_char();
      if (before.get_char() == '\n')
        starts.push_back(it.get_offset());
    }
    std::vector<TabLine> fresh(starts.size());
    paragraphs.resize(starts.size());
    for (size_t i = 0; i < starts.size(); ++i) {
      const ParaFormat format = para_at(starts[i]);
      fresh[i].format = format;
      paragraphs[i].list = format.list;
      paragraphs[i].indents = format.indents;
      paragraphs[i].align = format.align;
    }
    const std::vector<int> numbers = list_numbers(paragraphs);
    for (size_t i = 0; i < fresh.size(); ++i)
      fresh[i].number = numbers[i];
    dirty.assign(fresh.size(), 0);
    const bool moved = fresh.size() != tab_lines_.size();
    if (tabs_full_ || static_cast<int>(fresh.size()) != lines || (moved && !tabs_noted_)) {
      std::fill(dirty.begin(), dirty.end(), 1);
    } else {
      // Paragraphs whose format or number changed, matching the old list
      // from the top and from the bottom around the paragraphs added or
      // removed. The match stops at the edit: past it the text has moved,
      // so a paragraph at the old index may carry another one's tab, as
      // when Delete joins two items and every number below drops by one.
      auto same = [](const TabLine& a, const TabLine& b) {
        return a.format == b.format && a.number == b.number;
      };
      // With no edit noted, no paragraph came or went (else every one is
      // looked at above), so both matches may run the whole list.
      size_t head_max = std::min(fresh.size(), tab_lines_.size());
      size_t tail_max = head_max;
      if (tabs_noted_) {
        const int from = std::max(0, tabs_from_->get_iter().get_line());
        const int to = std::max(from, tabs_to_->get_iter().get_line());
        head_max = std::min(fresh.size(), static_cast<size_t>(from));
        tail_max = fresh.size() - std::min(fresh.size(), static_cast<size_t>(to) + 1);
      }
      size_t head = 0;
      while (head < head_max && head < tab_lines_.size() && same(fresh[head], tab_lines_[head]))
        ++head;
      size_t tail = 0;
      while (tail < tail_max && tail < fresh.size() - head && tail < tab_lines_.size() - head &&
             same(fresh[fresh.size() - 1 - tail], tab_lines_[tab_lines_.size() - 1 - tail]))
        ++tail;
      for (size_t i = head; i < fresh.size() - tail; ++i)
        dirty[i] = 1;
    }
    tab_lines_ = std::move(fresh);
  } else {
    dirty.assign(tab_lines_.size(), 0);
  }
  // The edited paragraphs, which a GTK line each when lines are paragraphs.
  if (tabs_noted_ && static_cast<int>(tab_lines_.size()) == lines) {
    const int from = tabs_from_->get_iter().get_line();
    const int to = tabs_to_->get_iter().get_line();
    for (int line = std::max(0, from); line <= to && line < lines; ++line)
      dirty[static_cast<size_t>(line)] = 1;
  }
  const bool full = tabs_full_;
  tabs_full_ = false;
  tabs_renumber_ = false;
  tabs_noted_ = false;
  if (std::find(dirty.begin(), dirty.end(), 1) == dirty.end())
    return;
  tabbing_ = true;
  std::vector<Glib::RefPtr<Gtk::TextTag>> used;
  for (size_t i = 0; i < dirty.size(); ++i) {
    if (!dirty[i])
      continue;
    const int start =
        walk ? starts[i] : buffer_->get_iter_at_line(static_cast<int>(i)).get_offset();
    if (start >= total)
      continue;
    int end = total;
    if (walk && i + 1 < starts.size())
      end = starts[i + 1];
    else if (!walk) {
      auto next = buffer_->get_iter_at_line(static_cast<int>(i));
      if (next.forward_line())
        end = next.get_offset();
    }
    // A paragraph whose own format changed renumbers: start again.
    if (!walk && !(para_at(start) == tab_lines_[i].format)) {
      tabbing_ = false;
      tabs_renumber_ = true;
      // The edited range still needs its look.
      tabs_noted_ = true;
      update_list_tabs();
      return;
    }
    auto want = retab_paragraph(i, start, end, old);
    if (want && std::find(used.begin(), used.end(), want) == used.end())
      used.push_back(want);
  }
  // After a full look, tags no paragraph needs go, so the table holds one
  // per indent in use.
  if (full) {
    for (const auto& tag : old)
      if (std::find(used.begin(), used.end(), tag) == used.end())
        table->remove(tag);
  }
  // A tab outranks every paragraph tag's indent. Raising a tag relays out
  // the text, so only when a paragraph tag sits above one.
  int top_para = -1;
  ParaFormat ignored;
  std::vector<Glib::RefPtr<Gtk::TextTag>> tabs;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (parse_para(tag_name(tag), ignored))
      top_para = std::max(top_para, tag->get_priority());
    else if (is_list_tab(tag_name(tag)))
      tabs.push_back(tag);
  });
  if (std::any_of(tabs.begin(), tabs.end(), [&](const Glib::RefPtr<Gtk::TextTag>& tag) {
        return tag->get_priority() < top_para;
      })) {
    for (const auto& tag : tabs)
      tag->set_priority(table->get_size() - 1);
    raise_headings();
  }
  tabbing_ = false;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::retab_paragraph(
    size_t index, int start, int end, const std::vector<Glib::RefPtr<Gtk::TextTag>>& old)
{
  ++list_tabs_evaluated_;
  const TabLine& line = tab_lines_[index];
  const ListFormat list = clamp_list(line.format.list);
  Glib::RefPtr<Gtk::TextTag> want;
  if (list.kind != ListKind::None &&
      (line.format.align == Align::Left || line.format.align == Align::Justify)) {
    // The label as drawn: list_label_layout() lays it out in the paragraph's
    // first character's font, size, weight, slant, and script, so the cache
    // keys on those, and the label's text. Script changes the width.
    const Run format = format_of(buffer_->get_iter_at_offset(start));
    const char script = format.script == Script::Super ? 'u'
                        : format.script == Script::Sub ? 'd'
                                                       : '-';
    const std::string key = (format.font.empty() ? "Sans" : format.font) + '\x1f' +
                            std::to_string(half_points_of(format.size)) + '\x1f' +
                            (format.bold ? 'b' : '-') + (format.italic ? 'i' : '-') + script +
                            '\x1f' + list_label(list, line.number);
    auto known = tab_widths_.find(key);
    if (known == tab_widths_.end()) {
      Paragraph paragraph;
      paragraph.list = line.format.list;
      int width = 0;
      int gap = 0;
      list_label_layout(paragraph, start, line.number, width, gap);
      known = tab_widths_.emplace(key, width).first;
    }
    const Indents indents = clamp_indents(line.format.indents);
    const int text_twips = list_text_start(indents);
    // As Word's tab after the label: the text stays put while the label
    // ends before it, at least a pixel clear.
    const int label_end =
        margin_left() + indent_px(indents.left + indents.first) + known->second + 1;
    if (label_end > margin_left() + indent_px(text_twips)) {
      // No stops of its own: the same half-inch grid, at the document's
      // interval. Stops of its own replace that grid up to the last one.
      const int interval = clamp_default_tab(default_tab_);
      int stop = 0;
      if (line.format.tabs.empty()) {
        stop = text_twips - ((text_twips % interval) + interval) % interval;
        while (margin_left() + indent_px(stop) < label_end && stop < 2 * kMaxIndent)
          stop += interval;
      } else {
        stop = tab_at(line.format.tabs, interval, text_twips);
        while (margin_left() + indent_px(stop) < label_end && stop < kMaxIndent) {
          const int next = tab_at(line.format.tabs, interval, stop + 1);
          if (next <= stop)
            break;
          stop = next;
        }
      }
      // Relative to the paragraph tag's left margin, as its indent is.
      want = list_tab_tag(margin_left() + indent_px(stop) -
                          std::max(0, margin_left() + indent_px(indents.left)));
    }
  }
  const auto s = buffer_->get_iter_at_offset(start);
  const auto e = buffer_->get_iter_at_offset(end);
  for (const auto& tag : old) {
    auto it = s;
    if (tag != want && (s.has_tag(tag) || (it.forward_to_tag_toggle(tag) && it.compare(e) < 0)))
      buffer_->remove_tag(tag, buffer_->get_iter_at_offset(start),
                          buffer_->get_iter_at_offset(end));
  }
  if (want) {
    auto it = s;
    if (!s.has_tag(want) || (it.forward_to_tag_toggle(want) && it.compare(e) < 0))
      buffer_->apply_tag(want, buffer_->get_iter_at_offset(start),
                         buffer_->get_iter_at_offset(end));
  }
  return want;
}

const std::vector<MainWindow::ListLine>& MainWindow::list_lines()
{
  if (list_lines_valid_ && list_lines_pending_set_ == pending_para_set_ &&
      (!pending_para_set_ || list_lines_pending_ == pending_para_))
    return list_lines_;
  list_lines_.clear();
  // Paragraphs start at 0 and after each '\n', as capture() splits them;
  // GTK's lines also break at other separators. After a final '\n' comes an
  // empty last paragraph, which forward_line() does not stop at.
  std::vector<int> starts{0};
  std::vector<int> lines{0};
  auto it = buffer_->begin();
  while (it.forward_line()) {
    auto before = it;
    before.backward_char();
    if (before.get_char() == '\n') {
      starts.push_back(it.get_offset());
      lines.push_back(it.get_line());
    }
  }
  const int total = buffer_->get_char_count();
  if (total > 0 && starts.back() != total &&
      buffer_->get_iter_at_offset(total - 1).get_char() == '\n') {
    starts.push_back(total);
    lines.push_back(buffer_->get_line_count() - 1);
  }
  // Each one's format by capture()'s rule: its first character with a
  // paragraph tag, else the held format, else the paragraph above's.
  std::vector<Paragraph> paragraphs(starts.size());
  list_lines_.resize(starts.size());
  for (size_t i = 0; i < starts.size(); ++i) {
    ParaFormat format;
    bool found = false;
    for (auto iter = buffer_->get_iter_at_offset(starts[i]); !iter.is_end(); ++iter) {
      if (auto tag = para_tag_at(iter)) {
        if (parse_para(tag_name(tag), format)) {
          found = true;
          break;
        }
      }
      if (iter.get_char() == '\n')
        break;
    }
    if (!found) {
      if (pending_para_set_)
        format = pending_para_;
      else if (i > 0)
        format = list_lines_[i - 1].format;
      else
        format = ParaFormat{};
    } else if (starts[i] < total) {
      // para_format() takes the basis from the first run or the paragraph
      // mark. A tag with default spacing does not store that size, so the
      // name still says 11 pt.
      auto text = buffer_->get_iter_at_offset(starts[i]);
      if (text.get_char() == '\n') {
        if (has_fmt(text)) {
          const int half = half_points_of(format_of(text).size);
          if (half >= 2)
            format.basis = half;
        }
      } else {
        for (; !text.is_end() && text.get_char() != '\n'; ++text) {
          if (text.get_char() == kPageBreakPad)
            continue;
          const int half = half_points_of(format_of(text).size);
          if (half >= 2)
            format.basis = half;
          break;
        }
      }
    }
    list_lines_[i].format = format;
    list_lines_[i].line = lines[i];
    paragraphs[i].list = format.list;
  }
  const std::vector<int> numbers = list_numbers(paragraphs);
  list_lines_any_ = false;
  list_lines_centred_ = false;
  for (size_t i = 0; i < list_lines_.size(); ++i) {
    const ParaFormat& format = list_lines_[i].format;
    list_lines_[i].number = numbers[i];
    if (format.list.kind != ListKind::None)
      list_lines_any_ = true;
    if (format.align == Align::Center && clamp_list(format.list).kind != ListKind::None)
      list_lines_centred_ = true;
  }
  list_lines_valid_ = true;
  list_lines_pending_set_ = pending_para_set_;
  list_lines_pending_ = pending_para_;
  return list_lines_;
}

bool MainWindow::any_list() const
{
  // Every paragraph's format is a tag's or the held one (capture()'s rule),
  // so with neither carrying a list there is none.
  if (pending_para_set_ && pending_para_.list.kind != ListKind::None)
    return true;
  bool any = false;
  const auto begin = buffer_->begin();
  buffer_->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (any)
      return;
    ParaFormat format;
    if (!parse_para(tag_name(tag), format) || format.list.kind == ListKind::None)
      return;
    auto it = begin;
    if (begin.has_tag(tag) || it.forward_to_tag_toggle(tag))
      any = true;
  });
  return any;
}

Paragraph MainWindow::label_paragraph(size_t index, int offset) const
{
  const ParaFormat& format = list_lines_[index].format;
  Paragraph paragraph;
  paragraph.indents = format.indents;
  paragraph.align = format.align;
  paragraph.list = format.list;
  paragraph.style = format.style;
  paragraph.direct = format.direct;
  paragraph.spacing = format.spacing;
  paragraph.basis = format.basis;
  paragraph.borders = format.borders;
  paragraph.shading = format.shading;
  // What capture() would give list_label_layout(): the first run's format,
  // an empty paragraph's mark, or neither.
  const auto iter = buffer_->get_iter_at_offset(offset);
  if (iter.is_end()) {
    if (pending_mark_set_)
      paragraph.mark = pending_mark_;
  } else if (iter.get_char() == '\n') {
    if (has_fmt(iter))
      paragraph.mark = format_of(iter);
  } else {
    paragraph.runs.push_back(format_of(iter));
  }
  return paragraph;
}

int MainWindow::natural_line_above(const Gtk::TextIter& iter) const
{
  // The gap GTK draws when no page seam replaces it: the highest tag that
  // sets pixels above, other than a seam. Default spacing leaves a heading
  // at its own gap and a draft sheet start at 18. A paragraph's space wins
  // when that tag was raised.
  int best = -1;
  int pixels = 0;
  for (const auto& tag : iter.get_tags()) {
    if (is_page_seam(tag_name(tag)))
      continue;
    if (!tag->property_pixels_above_lines_set())
      continue;
    if (tag->get_priority() >= best) {
      best = tag->get_priority();
      pixels = tag->property_pixels_above_lines();
    }
  }
  return pixels;
}

namespace {

int seam_at(const Gtk::TextIter& iter)
{
  int pixels = 0;
  for (const auto& tag : iter.get_tags())
    pixels = std::max(pixels, page_seam_pixels(tag_name(tag)));
  return pixels;
}

int note_of(const Gtk::TextIter& iter)
{
  Run run;
  for (const auto& tag : iter.get_tags()) {
    if (parse_fmt(tag_name(tag), run) && run.note != 0)
      return run.note;
  }
  return 0;
}

bool view_has_note(Gtk::TextView& view, const std::function<bool(int)>& want)
{
  auto buffer = view.get_buffer();
  if (!buffer)
    return false;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    const int note = note_of(iter);
    if (note != 0 && want(note))
      return true;
  }
  return false;
}

bool widget_has_note(Gtk::Widget& widget, const std::function<bool(int)>& want)
{
  if (auto* view = dynamic_cast<Gtk::TextView*>(&widget))
    return view_has_note(*view, want);
  auto* container = dynamic_cast<Gtk::Container*>(&widget);
  if (!container)
    return false;
  for (Gtk::Widget* child : container->get_children()) {
    if (child && widget_has_note(*child, want))
      return true;
  }
  return false;
}

// The offset just past this paragraph's '\n', or -1 when the paragraph runs
// to the end of the buffer. forward_line jumps there. forward_to_line_end
// does not: an empty paragraph's iterator is already on the newline, and
// that call moves to the next line's delimiter, so the seam for one page
// break was painted on every following blank line. GTK also breaks lines on
// separators other than '\n'. Those stay one paragraph, the same as the
// character scan this replaced.
int next_paragraph_offset(Gtk::TextIter iter)
{
  if (iter.is_end())
    return -1;
  const int start = iter.get_offset();
  auto next = iter;
  if (next.forward_line()) {
    auto before = next;
    if (before.backward_char() && before.get_offset() >= start && before.get_char() == '\n')
      return next.get_offset();
  }
  for (; !iter.is_end(); ++iter) {
    if (iter.get_char() == '\n') {
      iter.forward_char();
      return iter.get_offset();
    }
  }
  return -1;
}

void collect_widget_notes(Gtk::Widget& widget, const std::function<void(int)>& fn)
{
  if (auto* view = dynamic_cast<Gtk::TextView*>(&widget)) {
    auto buffer = view->get_buffer();
    if (!buffer)
      return;
    for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
      const int note = note_of(iter);
      if (note != 0)
        fn(note);
    }
    return;
  }
  auto* container = dynamic_cast<Gtk::Container*>(&widget);
  if (!container)
    return;
  for (Gtk::Widget* child : container->get_children()) {
    if (child)
      collect_widget_notes(*child, fn);
  }
}

}  // namespace

int MainWindow::seam_extra_through(int offset) const
{
  if (!buffer_)
    return 0;
  const int count = buffer_->get_char_count();
  const int at = std::max(0, std::min(offset, count));
  int extra = 0;
  auto iter = buffer_->begin();
  while (iter.get_offset() <= at) {
    const int seam = seam_at(iter);
    if (seam > 0)
      extra += std::max(0, seam - natural_line_above(iter));
    const int next = next_paragraph_offset(iter);
    if (next < 0)
      break;
    iter = buffer_->get_iter_at_offset(next);
  }
  return extra;
}

bool MainWindow::starts_sheet(int offset) const
{
  const ParaFormat format = para_at(offset);
  return format.page_break && format.cell.table == 0;
}

bool MainWindow::has_page_break() const
{
  if (!buffer_)
    return false;
  const int count = buffer_->get_char_count();
  for (int start = 0; start <= count;) {
    if (starts_sheet(start))
      return true;
    const int end = paragraph_end(start);
    if (end <= start)
      break;
    start = end;
  }
  return false;
}

int MainWindow::sheet_y(int offset) const
{
  // The same sheet decisions as update_page_seams(). Draft draws no seams and
  // does not paint the footnote, so the status bar counts from this. A sheet
  // that holds a marker gives up the same note height Page view would. A
  // document with no page break and no note stays on the laid-out height.
  if (!buffer_)
    return 0;
  struct Block {
    int start = 0;
    int end = 0;
    int top = 0;
    int text_h = 0;
    int natural = 0;
    int seam = 0;
    bool page_break = false;
  };
  std::vector<Block> blocks;
  int bottom_y = 0;
  int bottom_h = 0;
  text_.get_line_yrange(buffer_->end(), bottom_y, bottom_h);
  const int bottom = bottom_y + bottom_h;
  const int count = buffer_->get_char_count();
  int at = 0;
  while (at <= count) {
    auto iter = buffer_->get_iter_at_offset(at);
    int top = 0;
    int height = 0;
    text_.get_line_yrange(iter, top, height);
    Block block;
    block.start = at;
    block.top = top;
    block.natural = natural_line_above(iter);
    block.seam = seam_at(iter);
    const int next = next_paragraph_offset(iter);
    block.end = next < 0 ? count : next;
    block.page_break = starts_sheet(block.start);
    blocks.push_back(block);
    if (next < 0)
      break;
    at = next;
  }
  if (blocks.empty())
    return 0;
  for (size_t i = 0; i < blocks.size(); ++i) {
    const int next_top = i + 1 < blocks.size() ? blocks[i + 1].top : bottom;
    const int displayed = blocks[i].seam > 0 ? blocks[i].seam : blocks[i].natural;
    blocks[i].text_h = std::max(1, next_top - blocks[i].top - displayed);
  }
  const int per = std::max(1, page_text_height(page_setup_, zoom_factor()));
  const int where = std::max(0, std::min(offset, count));
  const int note_px = draft_footnote_px();
  // A body marker, the same scan Page view uses for a single note column.
  // A cut of 0 leaves the push test as it was.
  auto block_has_note = [&](int start, int end) {
    if (note_px <= 0)
      return false;
    const int from = std::max(0, start);
    const int to = std::max(from, std::min(end, count));
    for (int at = from; at < to; ++at) {
      Run run;
      for (const auto& tag : buffer_->get_iter_at_offset(at).get_tags()) {
        if (parse_fmt(tag_name(tag), run) && run.note != 0)
          return true;
      }
    }
    return false;
  };
  // `reserve_page` is the sheet that gives the note its band, or -1.
  auto paginate = [&](int reserve_page, std::vector<int>* page_of) {
    int y = 0;
    int page = 0;
    int result = 0;
    if (page_of)
      page_of->assign(blocks.size(), 0);
    for (size_t i = 0; i < blocks.size(); ++i) {
      const int text_h = blocks[i].text_h;
      const int box = blocks[i].natural + text_h;
      while (y >= (page + 1) * per)
        ++page;
      if (page_of)
        (*page_of)[i] = page;
      const int cut = reserve_page >= 0 && page == reserve_page ? note_px : 0;
      const FootnoteRoom fit = footnote_room(per, cut);
      const int limit = (page + 1) * per - fit.cut;
      const int page_edge = (page + 1) * per;
      const bool forced = blocks[i].page_break && blocks[i].start < blocks[i].end;
      const bool push = forced || (y > 0 && y + box > limit && text_h <= fit.room);
      const int visual = page + (push ? 1 : 0);
      const bool last = i + 1 == blocks.size();
      const bool inside = where >= blocks[i].start && (where < blocks[i].end || last);
      if (inside) {
        int within = 0;
        if (where > blocks[i].start && where < blocks[i].end) {
          Gdk::Rectangle loc;
          text_.get_iter_location(buffer_->get_iter_at_offset(where), loc);
          const int displayed = blocks[i].seam > 0 ? blocks[i].seam : blocks[i].natural;
          within = std::max(0, loc.get_y() - blocks[i].top - displayed);
        } else if (where >= blocks[i].end)
          within = text_h;
        result = visual * per + std::min(std::max(0, within), text_h);
      }
      if (push) {
        y = page_edge + text_h;
        ++page;
      } else
        y += box;
    }
    return result;
  };
  if (note_px <= 0)
    return paginate(-1, nullptr);
  std::vector<int> first_page;
  const int plain = paginate(-1, &first_page);
  int home = -1;
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (!block_has_note(blocks[i].start, blocks[i].end))
      continue;
    home = first_page[i];
    break;
  }
  if (home < 0)
    return plain;
  std::vector<int> again;
  const int reserved = paginate(home, &again);
  // Keep the reserve when the marker stays on that sheet. A block the note
  // itself pushes down is the next sheet, and the note stays put.
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (block_has_note(blocks[i].start, blocks[i].end) && again[i] == home)
      return reserved;
  }
  return plain;
}

int MainWindow::page_content_y(int layout_y, int offset) const
{
  if (!buffer_)
    return layout_y;
  const int count = buffer_->get_char_count();
  const int at = std::max(0, std::min(offset, count));
  const int content = layout_y - seam_extra_through(at);
  // Draft strips the seams. The count still follows the same pagination as
  // Page view, including ordinary text that overflows a sheet.
  if (view_ != ViewMode::Page)
    return sheet_y(at);
  // Each gray gap starts a sheet. The line's content y is short of that
  // sheet by whatever room the previous sheet did not fill, so a tall block
  // pushed onto the next page keeps the old page number until the caret has
  // moved that far down the new sheet.
  const int per = std::max(1, page_text_height(page_setup_, zoom_factor()));
  int seam_index = 0;
  int lag = 0;
  auto iter = buffer_->begin();
  while (iter.get_offset() <= at) {
    if (seam_at(iter) > 0) {
      Gdk::Rectangle loc;
      text_.get_iter_location(iter, loc);
      const int origin = loc.get_y() - seam_extra_through(iter.get_offset());
      ++seam_index;
      lag = std::max(0, seam_index * per - origin);
    }
    const int next = next_paragraph_offset(iter);
    if (next < 0)
      break;
    iter = buffer_->get_iter_at_offset(next);
  }
  return content + lag;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::page_seam_tag(int pixels)
{
  const Glib::ustring name = std::string("page-seam") + '\x1f' + std::to_string(pixels);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    tag->property_pixels_above_lines() = pixels;
    table->add(tag);
  }
  return tag;
}

void MainWindow::raise_seams()
{
  if (!buffer_)
    return;
  auto table = buffer_->get_tag_table();
  std::vector<Glib::RefPtr<Gtk::TextTag>> seams;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (is_page_seam(tag_name(tag)))
      seams.push_back(tag);
  });
  if (seams.empty())
    return;
  const int top = table->get_size() - 1;
  const bool low =
      std::any_of(seams.begin(), seams.end(),
                  [&](const Glib::RefPtr<Gtk::TextTag>& tag) { return tag->get_priority() < top; });
  if (!low)
    return;
  for (const auto& tag : seams)
    tag->set_priority(table->get_size() - 1);
}

void MainWindow::hide_extra_notes()
{
  for (auto& band : note_bands_) {
    if (band)
      band->box.hide();
  }
}

std::vector<int> MainWindow::cell_note_anchors() const
{
  std::vector<int> marks;
  if (closing_ || !buffer_ || notes_.empty())
    return marks;
  // A body marker wins, the same rule as note_marker_offset(). The cells are
  // already on the page, so this does not capture(): a capture from a seam
  // pass lays the page out again, and one while the window is closing reads
  // widgets that are going away.
  std::vector<char> in_body(notes_.size(), 0);
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    if (iter.get_child_anchor())
      continue;
    const int note = note_of(iter);
    if (note > 0 && note <= static_cast<int>(in_body.size()))
      in_body[static_cast<size_t>(note - 1)] = 1;
  }
  std::vector<char> seen(notes_.size(), 0);
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    auto anchor = iter.get_child_anchor();
    if (!anchor)
      continue;
    const int at = iter.get_offset();
    for (Gtk::Widget* widget : anchor->get_widgets()) {
      if (!widget)
        continue;
      collect_widget_notes(*widget, [&](int note) {
        if (note <= 0 || note > static_cast<int>(notes_.size()))
          return;
        const size_t slot = static_cast<size_t>(note - 1);
        if (in_body[slot] || seen[slot])
          return;
        seen[slot] = 1;
        marks.push_back(at);
      });
    }
  }
  return marks;
}

int MainWindow::note_column_width() const
{
  const ViewGeometry sheet = view_ == ViewMode::Page
                                 ? geometry()
                                 : view_geometry(ViewMode::Page, zoom_factor(), page_setup_);
  return std::max(40, sheet.page_width - sheet.margin_left - sheet.margin_right);
}

int MainWindow::note_view_width(int column, const Gtk::Widget& marks) const
{
  int min_w = 0;
  int nat_w = 0;
  marks.get_preferred_width(min_w, nat_w);
  const int gutter = std::max(min_w, nat_w) + marks.get_margin_left() + marks.get_margin_right();
  return std::max(40, column - std::max(0, gutter));
}

void MainWindow::fit_note_views()
{
  const int column = note_column_width();
  notes_view_.set_layout_width(note_view_width(column, note_marks_));
  for (const auto& band : note_bands_) {
    if (!band || !band->wired)
      continue;
    band->view.set_layout_width(note_view_width(column, band->marks));
  }
}

int MainWindow::picture_column_limit() const
{
  const ViewGeometry sheet = geometry();
  return std::max(1, sheet.page_width - sheet.margin_left - sheet.margin_right);
}

int MainWindow::picture_cell_limit(int column_px) const
{
  // The frame border is 1px a side and the cell text keeps a 4px margin a
  // side. A picture is one character, so it does not wrap inside that.
  return std::max(1, column_px - 10);
}

// Show the numbers for this column and hide the rest. A column that used to
// hold every note keeps those labels; showing them all stacks the later
// numbers on the first footnote.
void show_note_marks(Gtk::Fixed& marks, Gtk::Label& first, const std::vector<Gtk::Label*>& labels,
                     int count)
{
  marks.show();
  if (labels.empty()) {
    if (count > 0)
      first.show();
    else
      first.hide();
    return;
  }
  for (size_t i = 0; i < labels.size(); ++i) {
    Gtk::Label* label = labels[i];
    if (!label)
      continue;
    if (static_cast<int>(i) < count)
      label->show();
    else
      label->hide();
  }
}

void MainWindow::place_notes()
{
  if (closing_)
    return;
  if (notes_.empty() || view_ != ViewMode::Page || !buffer_) {
    note_box_.hide();
    hide_extra_notes();
    return;
  }
  // Every marker on one sheet keeps the single column this has always drawn.
  fit_note_views();
  if (note_groups_.size() > 1) {
    place_split_notes();
    return;
  }
  hide_extra_notes();
  const ViewGeometry sheet = geometry();
  const int width = note_column_width();
  int asked_w = 0;
  int asked_h = 0;
  note_box_.get_size_request(asked_w, asked_h);
  if (asked_w != width)
    note_box_.set_size_request(width, -1);
  const int note_h =
      clamp_note_height(note_block_px(), std::max(1, page_text_height(page_setup_, zoom_factor())));
  int buffer_y = -1;
  int marker_y = -1;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    Run run;
    bool marked = false;
    for (const auto& tag : iter.get_tags()) {
      if (parse_fmt(tag_name(tag), run) && run.note != 0) {
        marked = true;
        break;
      }
    }
    if (!marked)
      continue;
    int top = 0;
    int height = 0;
    text_.get_line_yrange(iter, top, height);
    marker_y = top;
    break;
  }
  // A marker that lives only in a cell is not a body tag. The table anchor
  // is that marker's place, so the note uses the anchor's sheet.
  if (marker_y < 0) {
    const std::vector<int> anchors = cell_note_anchors();
    if (!anchors.empty()) {
      int at = anchors.front();
      for (int mark : anchors)
        at = std::min(at, mark);
      int top = 0;
      int height = 0;
      text_.get_line_yrange(buffer_->get_iter_at_offset(at), top, height);
      marker_y = top;
    }
  }
  if (marker_y >= 0) {
    auto iter = buffer_->begin();
    while (!iter.is_end()) {
      const int seam = seam_at(iter);
      if (seam > 0) {
        int top = 0;
        int height = 0;
        text_.get_line_yrange(iter, top, height);
        if (top > marker_y) {
          const int chrome = sheet.margin_bottom + sheet.gap + sheet.margin_y;
          buffer_y = top + seam - chrome - note_h;
          break;
        }
      }
      bool more = false;
      while (!iter.is_end()) {
        if (iter.get_char() == '\n') {
          iter.forward_char();
          more = !iter.is_end();
          break;
        }
        iter.forward_char();
      }
      if (!more)
        break;
    }
  }
  const int left = sheet.margin_left;
  int top_px = 0;
  if (buffer_y >= 0) {
    int wx = 0;
    int wy = 0;
    text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, buffer_y, wx, wy);
    int ox = 0;
    int oy = 0;
    if (text_.translate_coordinates(page_overlay_, wx, wy, ox, oy))
      top_px = oy;
  } else {
    int ox = 0;
    int oy = 0;
    const int foot = std::max(0, text_.get_allocated_height() - sheet.margin_bottom - note_h);
    if (text_.translate_coordinates(page_overlay_, 0, foot, ox, oy))
      top_px = oy;
  }
  if (top_px < 0)
    top_px = 0;
  // GtkOverlay puts a child's margin outside a window it has already sized to
  // the child. A margin taller than the note collapses that window, so the
  // position is the overlay's get-child-position allocation instead.
  const bool moved = note_x_ != left || note_y_ != top_px || note_w_ != width || note_h_ != note_h;
  note_x_ = left;
  note_y_ = top_px;
  note_w_ = width;
  note_h_ = std::max(1, note_h);
  // no_show_all keeps the window's show_all off an empty note. show_all on
  // this box itself then returns without showing it, so show the pieces.
  show_note_marks(note_marks_, note_label_, note_numbers_, static_cast<int>(notes_.size()));
  notes_view_.show();
  note_box_.show();
  if (moved)
    page_overlay_.queue_resize();
}

void MainWindow::place_split_notes()
{
  const ViewGeometry sheet = geometry();
  const int width = note_column_width();
  fit_note_views();
  const int left = sheet.margin_left;
  const int count = static_cast<int>(note_groups_.size());
  const int page_h = std::max(1, page_text_height(page_setup_, zoom_factor()));
  std::vector<int> height(static_cast<size_t>(count), 1);
  std::vector<int> marker(static_cast<size_t>(count), -1);
  int pages = 0;
  for (int i = 0; i < count; ++i) {
    const NoteGroup& group = note_groups_[static_cast<size_t>(i)];
    height[static_cast<size_t>(i)] =
        std::max(1, clamp_note_height(measure_note_group(group), page_h));
    pages = std::max(pages, group.page + 1);
    auto want = [&](int note) {
      return std::find(group.indexes.begin(), group.indexes.end(), note - 1) != group.indexes.end();
    };
    for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
      if (!want(note_of(iter)))
        continue;
      int top = 0;
      int line_h = 0;
      text_.get_line_yrange(iter, top, line_h);
      marker[static_cast<size_t>(i)] = top;
      break;
    }
    if (marker[static_cast<size_t>(i)] >= 0)
      continue;
    for (auto iter = buffer_->begin(); marker[static_cast<size_t>(i)] < 0 && !iter.is_end();
         ++iter) {
      auto anchor = iter.get_child_anchor();
      if (!anchor)
        continue;
      bool hit = false;
      for (Gtk::Widget* widget : anchor->get_widgets()) {
        if (widget && widget_has_note(*widget, want))
          hit = true;
      }
      if (!hit)
        continue;
      int top = 0;
      int line_h = 0;
      text_.get_line_yrange(iter, top, line_h);
      marker[static_cast<size_t>(i)] = top;
    }
  }

  std::vector<int> stack_top(static_cast<size_t>(std::max(pages, 1)), -1);
  std::vector<int> used(static_cast<size_t>(std::max(pages, 1)), 0);
  for (int i = 0; i < count; ++i) {
    const int page = note_groups_[static_cast<size_t>(i)].page;
    if (page < 0 || page >= pages)
      continue;
    used[static_cast<size_t>(page)] += height[static_cast<size_t>(i)];
    int& top = stack_top[static_cast<size_t>(page)];
    const int at = marker[static_cast<size_t>(i)];
    if (at >= 0 && (top < 0 || at < top))
      top = at;
  }

  auto overlay_top = [&](int marker_y, int note_h) {
    int buffer_y = -1;
    if (marker_y >= 0) {
      auto iter = buffer_->begin();
      while (!iter.is_end()) {
        const int seam = seam_at(iter);
        if (seam > 0) {
          int top = 0;
          int line_h = 0;
          text_.get_line_yrange(iter, top, line_h);
          if (top > marker_y) {
            const int chrome = sheet.margin_bottom + sheet.gap + sheet.margin_y;
            buffer_y = top + seam - chrome - note_h;
            break;
          }
        }
        bool more = false;
        while (!iter.is_end()) {
          if (iter.get_char() == '\n') {
            iter.forward_char();
            more = !iter.is_end();
            break;
          }
          iter.forward_char();
        }
        if (!more)
          break;
      }
    }
    int top_px = 0;
    if (buffer_y >= 0) {
      int wx = 0;
      int wy = 0;
      text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, buffer_y, wx, wy);
      int ox = 0;
      int oy = 0;
      if (text_.translate_coordinates(page_overlay_, wx, wy, ox, oy))
        top_px = oy;
    } else {
      int ox = 0;
      int oy = 0;
      const int foot = std::max(0, text_.get_allocated_height() - sheet.margin_bottom - note_h);
      if (text_.translate_coordinates(page_overlay_, 0, foot, ox, oy))
        top_px = oy;
    }
    return std::max(0, top_px);
  };

  std::vector<int> next_y(static_cast<size_t>(std::max(pages, 1)), 0);
  for (int page = 0; page < pages; ++page) {
    if (used[static_cast<size_t>(page)] <= 0)
      continue;
    next_y[static_cast<size_t>(page)] =
        overlay_top(stack_top[static_cast<size_t>(page)], used[static_cast<size_t>(page)]);
  }

  bool moved = false;
  for (int i = 0; i < count; ++i) {
    const int page = std::max(0, note_groups_[static_cast<size_t>(i)].page);
    const int y = page < pages ? next_y[static_cast<size_t>(page)] : 0;
    if (page < pages)
      next_y[static_cast<size_t>(page)] += height[static_cast<size_t>(i)];
    const std::string text = note_group_text(note_groups_[static_cast<size_t>(i)]);
    if (i == 0) {
      show_note_text(notes_view_, text);
      int asked_w = 0;
      int asked_h = 0;
      note_box_.get_size_request(asked_w, asked_h);
      if (asked_w != width)
        note_box_.set_size_request(width, -1);
      if (note_x_ != left || note_y_ != y || note_w_ != width || note_h_ != height[0])
        moved = true;
      note_x_ = left;
      note_y_ = y;
      note_w_ = width;
      note_h_ = height[0];
      show_note_marks(note_marks_, note_label_, note_numbers_,
                      static_cast<int>(note_groups_[static_cast<size_t>(i)].indexes.size()));
      notes_view_.show();
      note_box_.show();
      continue;
    }
    NoteBand& band = ensure_note_band(static_cast<size_t>(i - 1));
    show_note_text(band.view, text);
    int asked_w = 0;
    int asked_h = 0;
    band.box.get_size_request(asked_w, asked_h);
    if (asked_w != width)
      band.box.set_size_request(width, -1);
    const int band_h = height[static_cast<size_t>(i)];
    if (band.x != left || band.y != y || band.w != width || band.h != band_h)
      moved = true;
    band.x = left;
    band.y = y;
    band.w = width;
    band.h = band_h;
    show_note_marks(band.marks, band.label, band.numbers,
                    static_cast<int>(note_groups_[static_cast<size_t>(i)].indexes.size()));
    band.view.show();
    band.box.show();
  }
  for (size_t extra = static_cast<size_t>(std::max(0, count - 1)); extra < note_bands_.size();
       ++extra) {
    if (note_bands_[extra])
      note_bands_[extra]->box.hide();
  }
  // Asking the numbers to align, or the overlay to resize, when the band did
  // not move schedules another seam pass. That pass finds the same band and
  // asks again, so the page never settles.
  if (moved) {
    queue_note_numbers();
    page_overlay_.queue_resize();
  }
}

void MainWindow::update_page_seams()
{
  if (closing_ || seaming_ || !buffer_)
    return;
  // Draft is one column. The sheet seams belong to Page only. A page break
  // still opens the short gap, including on a heading: the heading's own
  // space would win, and the gap would never show.
  if (view_ != ViewMode::Page) {
    std::vector<Glib::RefPtr<Gtk::TextTag>> drop;
    buffer_->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
      if (is_page_seam(tag_name(tag)))
        drop.push_back(tag);
    });
    bool changed = false;
    if (!drop.empty()) {
      seaming_ = true;
      for (const auto& tag : drop)
        buffer_->remove_tag(tag, buffer_->begin(), buffer_->end());
      for (const auto& tag : drop)
        buffer_->get_tag_table()->remove(tag);
      seaming_ = false;
      changed = true;
    }
    if (sync_draft_gaps())
      changed = true;
    if (changed) {
      grow_page_ = true;
      queue_page_size();
    }
    return;
  }
  sync_draft_gaps();
  if (text_.get_allocated_width() <= 1)
    return;
  ++page_walks_;
  // Measure the notes at the width they will wrap, not the width they had
  // at the previous zoom.
  fit_note_views();

  struct Block {
    int start = 0;
    int end = 0;
    int top = 0;
    int text_h = 0;
    int natural = 0;
    int seam = 0;
    bool page_break = false;
  };
  std::vector<Block> blocks;
  int bottom_y = 0;
  int bottom_h = 0;
  text_.get_line_yrange(buffer_->end(), bottom_y, bottom_h);
  const int bottom = bottom_y + bottom_h;
  int offset = 0;
  const int count = buffer_->get_char_count();
  while (offset <= count) {
    auto iter = buffer_->get_iter_at_offset(offset);
    int top = 0;
    int height = 0;
    text_.get_line_yrange(iter, top, height);
    Block block;
    block.start = offset;
    block.top = top;
    block.natural = natural_line_above(iter);
    block.seam = seam_at(iter);
    const int next = next_paragraph_offset(iter);
    block.end = next < 0 ? count : next;
    block.page_break = starts_sheet(block.start);
    blocks.push_back(block);
    if (next < 0)
      break;
    offset = next;
  }
  if (blocks.empty())
    return;
  for (size_t i = 0; i < blocks.size(); ++i) {
    const int next_top = i + 1 < blocks.size() ? blocks[i + 1].top : bottom;
    const int displayed = blocks[i].seam > 0 ? blocks[i].seam : blocks[i].natural;
    blocks[i].text_h = std::max(1, next_top - blocks[i].top - displayed);
  }

  const ViewGeometry sheet = geometry();
  const int per = std::max(1, page_text_height(page_setup_, zoom_factor()));
  const int chrome = sheet.margin_bottom + sheet.gap + sheet.margin_y;
  const int reserve_px = note_block_px();
  const std::vector<int> cell_marks = cell_note_anchors();
  auto has_note = [&](int start, int end) {
    const int from = std::max(0, start);
    const int to = std::max(from, std::min(end, count));
    for (int at = from; at < to; ++at) {
      Run run;
      for (const auto& tag : buffer_->get_iter_at_offset(at).get_tags()) {
        if (parse_fmt(tag_name(tag), run) && run.note != 0)
          return true;
      }
    }
    return false;
  };
  // The body scan above is the shipped test for a body marker. A cell note
  // has no body tag; the anchor offset is the marker, and only when the body
  // has none.
  auto has_cell_note = [&](int start, int end) {
    const int from = std::max(0, start);
    const int to = std::max(from, std::min(end, count));
    for (int at : cell_marks) {
      if (at >= from && at < to)
        return true;
    }
    return false;
  };
  // Pages break as they always have when nothing is reserved. A sheet that
  // holds a footnote gives that note's band back at its foot. One sheet
  // still reserves the whole column for the first marker, as before.
  auto place = [&](const std::vector<int>& reserve, std::vector<int>& page_of) {
    page_of.assign(blocks.size(), 0);
    std::vector<int> placed(blocks.size(), 0);
    int y = 0;
    int page = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
      const int text_h = blocks[i].text_h;
      const int box = blocks[i].natural + text_h;
      while (y >= (page + 1) * per)
        ++page;
      page_of[i] = page;
      const int cut = page >= 0 && static_cast<size_t>(page) < reserve.size()
                          ? reserve[static_cast<size_t>(page)]
                          : 0;
      const int limit = (page + 1) * per - cut;
      const int room = std::max(1, per - cut);
      const int page_edge = (page + 1) * per;
      // A paragraph that does not fit in the room left on this sheet starts
      // the next one. One taller than that room stays put and crosses the
      // edge, unless the note itself is taller than the sheet. A page break
      // starts a sheet even at the top of one, and even when the paragraph
      // is taller than the sheet. An empty block has no character for the
      // seam, so it is not forced here.
      const bool forced = blocks[i].page_break && blocks[i].start < blocks[i].end;
      const bool moves = line_moves_for_note(text_h, room, cut, per);
      if (forced || (y > 0 && y + box > limit && moves)) {
        placed[i] = std::max(0, page_edge - y) + chrome;
        y = page_edge + text_h;
        ++page;
      } else {
        y += box;
      }
    }
    return placed;
  };
  // page_of is the sheet the layout had reached. A seam on the block puts
  // its text on the next sheet, which is the page the marker is drawn on.
  auto visual_page = [](const std::vector<int>& pages, const std::vector<int>& placed,
                        size_t block) {
    int page = pages[block];
    if (placed[block] > 0)
      ++page;
    return page;
  };
  auto sheets_for = [&](const std::vector<int>& pages, const std::vector<int>& placed) {
    std::vector<int> sheet(notes_.size(), -1);
    std::vector<char> marked(notes_.size(), 0);
    // No marker can claim a sheet. The two buffer walks below only exist to
    // find those markers.
    if (notes_.empty())
      return std::make_pair(sheet, marked);
    auto page_at = [&](int offset) {
      for (size_t i = 0; i < blocks.size(); ++i) {
        const bool inside = offset >= blocks[i].start &&
                            (offset < blocks[i].end || blocks[i].start == blocks[i].end);
        if (inside)
          return visual_page(pages, placed, i);
      }
      if (blocks.empty())
        return 0;
      return visual_page(pages, placed, blocks.size() - 1);
    };
    auto claim = [&](int note, int page) {
      if (note <= 0 || note > static_cast<int>(sheet.size()))
        return;
      int& slot = sheet[static_cast<size_t>(note - 1)];
      if (slot >= 0)
        return;
      slot = page;
      marked[static_cast<size_t>(note - 1)] = 1;
    };
    for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
      const int note = note_of(iter);
      if (note != 0)
        claim(note, page_at(iter.get_offset()));
    }
    for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
      auto anchor = iter.get_child_anchor();
      if (!anchor)
        continue;
      const int page = page_at(iter.get_offset());
      for (Gtk::Widget* widget : anchor->get_widgets()) {
        if (!widget)
          continue;
        collect_widget_notes(*widget, [&](int note) { claim(note, page); });
      }
    }
    int carry = -1;
    for (int& page : sheet) {
      if (page >= 0)
        carry = page;
      else if (carry >= 0)
        page = carry;
    }
    for (int& page : sheet) {
      if (page < 0)
        page = 0;
    }
    return std::make_pair(sheet, marked);
  };
  auto groups_for = [&](const std::vector<int>& sheet) {
    std::vector<NoteGroup> groups;
    for (int i = 0; i < static_cast<int>(sheet.size()); ++i) {
      if (groups.empty() || groups.back().page != sheet[static_cast<size_t>(i)]) {
        NoteGroup group;
        group.page = sheet[static_cast<size_t>(i)];
        groups.push_back(std::move(group));
      }
      groups.back().indexes.push_back(i);
    }
    return groups;
  };
  std::vector<int> page_of;
  std::vector<int> want = place({}, page_of);
  const auto first = sheets_for(page_of, want);
  const std::vector<NoteGroup> found = groups_for(first.first);
  if (found.size() <= 1) {
    note_groups_.clear();
    if (reserve_px > 0) {
      int home = -1;
      bool cell_home = false;
      for (size_t i = 0; i < blocks.size(); ++i) {
        if (!has_note(blocks[i].start, blocks[i].end))
          continue;
        home = page_of[i];
        break;
      }
      if (home < 0) {
        for (size_t i = 0; i < blocks.size(); ++i) {
          if (!has_cell_note(blocks[i].start, blocks[i].end))
            continue;
          home = page_of[i];
          cell_home = true;
          break;
        }
      }
      if (home >= 0) {
        std::vector<int> reserve(static_cast<size_t>(home) + 1, 0);
        reserve[static_cast<size_t>(home)] = reserve_px;
        std::vector<int> again;
        const std::vector<int> reserved = place(reserve, again);
        // Keep the break when the marker stays on that sheet. A block that the
        // note itself pushes down is the next sheet, and the note stays put.
        // A body marker uses the body scan. A cell note uses the anchor.
        bool stayed = false;
        for (size_t i = 0; i < blocks.size(); ++i) {
          const bool here = cell_home ? has_cell_note(blocks[i].start, blocks[i].end)
                                      : has_note(blocks[i].start, blocks[i].end);
          if (here && again[i] == home) {
            stayed = true;
            break;
          }
        }
        if (stayed)
          want = reserved;
      }
    }
  } else {
    int pages = 1;
    for (int s : first.first)
      pages = std::max(pages, s + 1);
    std::vector<int> reserve(static_cast<size_t>(pages), 0);
    for (const NoteGroup& group : found) {
      if (group.page < 0 || group.page >= pages)
        continue;
      reserve[static_cast<size_t>(group.page)] += measure_note_group(group);
    }
    std::vector<int> again;
    std::vector<int> reserved = place(reserve, again);
    auto moved = sheets_for(again, reserved);
    bool dropped = false;
    std::vector<char> keep(found.size(), 1);
    for (size_t g = 0; g < found.size(); ++g) {
      bool any = false;
      bool stayed = false;
      for (int index : found[g].indexes) {
        if (index < 0 || index >= static_cast<int>(first.second.size()) ||
            !first.second[static_cast<size_t>(index)])
          continue;
        any = true;
        if (index < static_cast<int>(moved.first.size()) &&
            moved.first[static_cast<size_t>(index)] == found[g].page)
          stayed = true;
      }
      if (any && !stayed) {
        keep[g] = 0;
        dropped = true;
      }
    }
    if (dropped) {
      std::fill(reserve.begin(), reserve.end(), 0);
      for (size_t g = 0; g < found.size(); ++g) {
        if (!keep[g])
          continue;
        const int page = found[g].page;
        if (page < 0 || page >= pages)
          continue;
        reserve[static_cast<size_t>(page)] += measure_note_group(found[g]);
      }
      reserved = place(reserve, again);
      moved = sheets_for(again, reserved);
    }
    want = reserved;
    note_groups_ = groups_for(moved.first);
    if (note_groups_.size() <= 1)
      note_groups_.clear();
  }
  // The empty last paragraph has no character a tag can sit on. Leaving it
  // unseamed keeps the gap off a range GTK would ignore, which would reapply
  // on every layout. An empty page break carries a zero-width space so its
  // seam has a character to sit on.
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (blocks[i].start == blocks[i].end)
      want[i] = 0;
  }

  bool changed = false;
  seaming_ = true;
  for (size_t i = 0; i < blocks.size(); ++i) {
    if (want[i] == blocks[i].seam)
      continue;
    auto from = buffer_->get_iter_at_offset(blocks[i].start);
    auto to = buffer_->get_iter_at_offset(blocks[i].end);
    std::vector<Glib::RefPtr<Gtk::TextTag>> drop;
    for (const auto& tag : from.get_tags()) {
      if (is_page_seam(tag_name(tag)))
        drop.push_back(tag);
    }
    for (const auto& tag : drop)
      buffer_->remove_tag(tag, from, to);
    if (want[i] > 0)
      buffer_->apply_tag(page_seam_tag(want[i]), from, to);
    changed = true;
  }
  if (changed) {
    std::vector<int> used = want;
    std::vector<Glib::RefPtr<Gtk::TextTag>> idle;
    auto table = buffer_->get_tag_table();
    table->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
      const int pixels = page_seam_pixels(tag_name(tag));
      if (pixels > 0 && std::find(used.begin(), used.end(), pixels) == used.end())
        idle.push_back(tag);
    });
    for (const auto& tag : idle)
      table->remove(tag);
    raise_seams();
  }
  seaming_ = false;
  if (changed) {
    grow_page_ = true;
    queue_page_size();
  }
  place_notes();
}

void MainWindow::draw_page_seams(const Cairo::RefPtr<Cairo::Context>& cr)
{
  if (!buffer_ || view_ != ViewMode::Page)
    return;
  const ViewGeometry sheet = geometry();
  if (sheet.gap <= 0)
    return;
  Gdk::Rectangle visible;
  text_.get_visible_rect(visible);
  double clip_x1 = 0;
  double clip_y1 = 0;
  double clip_x2 = 0;
  double clip_y2 = 0;
  cr->get_clip_extents(clip_x1, clip_y1, clip_x2, clip_y2);
  const int view_top = std::max(visible.get_y(), static_cast<int>(std::floor(clip_y1)));
  const int view_bottom =
      std::min(visible.get_y() + visible.get_height(), static_cast<int>(std::ceil(clip_y2)));
  const int width = text_.get_allocated_width();
  auto iter = buffer_->begin();
  while (true) {
    const int seam = seam_at(iter);
    if (seam > 0) {
      int top = 0;
      int height = 0;
      text_.get_line_yrange(iter, top, height);
      // GTK paints a selected first line through its pixels-above. The seam
      // is that margin, so the highlight runs up the previous sheet and stops
      // only when it meets the line above. The page is white there. The gray
      // gap is painted over this.
      int cover_x = 0;
      int cover_y = 0;
      text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, top, cover_x, cover_y);
      int text_x = 0;
      int text_y = 0;
      text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, top + seam, text_x, text_y);
      if (text_y > cover_y && text_y >= view_top && cover_y <= view_bottom) {
        const int y0 = std::max(cover_y, view_top);
        const int y1 = std::min(text_y, view_bottom);
        if (y1 > y0) {
          cr->set_source_rgb(1, 1, 1);
          cr->rectangle(0, y0, width, y1 - y0);
          cr->fill();
        }
      }
      // [pad][bottom margin][gray][top margin]. The gray is the pasteboard.
      const int gray_y = top + seam - sheet.gap - sheet.margin_y;
      int window_x = 0;
      int window_y = 0;
      text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, gray_y, window_x, window_y);
      if (window_y + sheet.gap >= view_top && window_y <= view_bottom) {
        cr->set_source_rgb(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0);
        cr->rectangle(0, window_y, width, sheet.gap);
        cr->fill();
        cr->set_source_rgb(92.0 / 255.0, 92.0 / 255.0, 92.0 / 255.0);
        cr->set_line_width(1);
        cr->move_to(0, window_y + 0.5);
        cr->line_to(width, window_y + 0.5);
        cr->move_to(0, window_y + sheet.gap - 0.5);
        cr->line_to(width, window_y + sheet.gap - 0.5);
        cr->stroke();
      }
    }
    // The next paragraph. A seam sits on the paragraph's first character, so
    // the characters in between are not visited.
    Gtk::TextIter match_start;
    Gtk::TextIter match_end;
    if (!iter.forward_search("\n", Gtk::TEXT_SEARCH_TEXT_ONLY, match_start, match_end) ||
        match_end.is_end())
      break;
    iter = match_end;
  }
}

void MainWindow::draw_paragraph_borders(const Cairo::RefPtr<Cairo::Context>& cr)
{
  if (!buffer_)
    return;
  // A plain document has no "parab" tag. One walk of the tag table, then
  // nothing else, so typing does not measure every paragraph.
  bool any = false;
  buffer_->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (any)
      return;
    const std::string name = tag_name(tag);
    if ((name.size() >= 5 && name.compare(0, 5, "parab") == 0) ||
        (name.size() >= 6 && name.compare(0, 6, "paratb") == 0))
      any = true;
  });
  if (!any)
    return;
  Gdk::Rectangle visible;
  text_.get_visible_rect(visible);
  double clip_x1 = 0;
  double clip_y1 = 0;
  double clip_x2 = 0;
  double clip_y2 = 0;
  cr->get_clip_extents(clip_x1, clip_y1, clip_x2, clip_y2);
  int ignored = 0;
  int clip_top = 0;
  int clip_bottom = 0;
  text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, static_cast<int>(std::floor(clip_y1)),
                                ignored, clip_top);
  text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, static_cast<int>(std::ceil(clip_y2)),
                                ignored, clip_bottom);
  const int view_top = std::max(visible.get_y(), clip_top);
  const int view_bottom = std::min(visible.get_y() + visible.get_height(), clip_bottom);
  if (view_bottom < view_top)
    return;
  Gtk::TextIter top;
  int top_y = 0;
  text_.get_line_at_y(top, view_top, top_y);
  const int count = buffer_->get_char_count();
  const double zoom = zoom_factor();
  const int width = text_.get_allocated_width();
  const ViewGeometry sheet = geometry();
  // The seam (or draft's short gap) is pixels-above on the first line. It is
  // the sheet above and the pasteboard, not part of the paragraph.
  auto leading_px = [](const Gtk::TextIter& at) {
    const int seam = seam_at(at);
    if (seam > 0)
      return seam;
    int gap = 0;
    for (const auto& tag : at.get_tags()) {
      if (!is_draft_gap(tag_name(tag)) || !tag->property_pixels_above_lines_set())
        continue;
      gap = std::max(gap, static_cast<int>(tag->property_pixels_above_lines()));
    }
    return gap;
  };
  for (int offset = paragraph_start(top.get_offset()); offset < count;) {
    const int end = paragraph_end(offset);
    auto iter = buffer_->get_iter_at_offset(offset);
    int y = 0;
    int height = 0;
    text_.get_line_yrange(iter, y, height);
    if (y > view_bottom)
      break;
    ParaFormat format;
    if (auto tag = para_tag_at(iter))
      parse_para(tag_name(tag), format);
    if (format.cell.table == 0 && !borders_plain(format.borders)) {
      auto last = buffer_->get_iter_at_offset(end > offset ? end - 1 : offset);
      int last_y = 0;
      int last_height = 0;
      text_.get_line_yrange(last, last_y, last_height);
      const Borders borders = clamp_borders(format.borders);
      const double left =
          margin_left() + indent_px(format.indents.left) - indent_px(borders.left.space);
      const double right =
          width - margin_right() - indent_px(format.indents.right) + indent_px(borders.right.space);
      int window_x = 0;
      int window_top = 0;
      int window_bottom = 0;
      const int top_buf =
          border_top_y(y, leading_px(iter), indent_px(borders.top.space), sheet.margin_y);
      text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, top_buf, window_x, window_top);
      text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, last_y + last_height, window_x,
                                    window_bottom);
      const double top_edge = window_top;
      const double bottom_edge = window_bottom + indent_px(borders.bottom.space);
      if (right > left && bottom_edge > top_edge && window_bottom >= view_top)
        paint_borders(cr, left, top_edge, right, bottom_edge, borders, zoom);
    }
    if (end <= offset)
      break;
    offset = end;
  }
}

void MainWindow::draw_cell_paragraph_borders(Gtk::TextView& view,
                                             const Cairo::RefPtr<Cairo::Context>& cr)
{
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  bool any = false;
  buffer->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (any)
      return;
    const std::string name = tag_name(tag);
    if ((name.size() >= 6 && name.compare(0, 6, "cparab") == 0) ||
        (name.size() >= 7 && name.compare(0, 7, "cparatb") == 0))
      any = true;
  });
  if (!any)
    return;
  const int count = buffer->get_char_count();
  const double zoom = zoom_factor();
  const int width = view.get_allocated_width();
  for (int offset = 0; offset <= count;) {
    auto iter = buffer->get_iter_at_offset(std::min(offset, count));
    Indents indents;
    Align align = Align::Left;
    Spacing spacing;
    int basis = 22;
    int heading = 0;
    Borders borders;
    Shading shading;
    std::vector<TabStop> cell_tabs;
    bool found = false;
    for (const auto& tag : iter.get_tags()) {
      if (parse_cpara(tag_name(tag), indents, align, spacing, basis, heading, borders, shading,
                      cell_tabs)) {
        found = true;
        break;
      }
    }
    (void)cell_tabs;
    int end = offset;
    auto cursor = buffer->get_iter_at_offset(std::min(offset, count));
    while (!cursor.is_end()) {
      const bool newline = cursor.get_char() == '\n';
      cursor.forward_char();
      end = cursor.get_offset();
      if (newline)
        break;
    }
    if (found && !borders_plain(borders)) {
      int y = 0;
      int height = 0;
      view.get_line_yrange(buffer->get_iter_at_offset(std::min(offset, count)), y, height);
      auto last = buffer->get_iter_at_offset(end > offset ? end - 1 : offset);
      int last_y = 0;
      int last_height = 0;
      view.get_line_yrange(last, last_y, last_height);
      const Borders box = clamp_borders(borders);
      const double left =
          view.get_left_margin() + indent_px(indents.left) - indent_px(box.left.space);
      const double right =
          width - view.get_right_margin() - indent_px(indents.right) + indent_px(box.right.space);
      int window_x = 0;
      int window_top = 0;
      int window_bottom = 0;
      view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, y, window_x, window_top);
      view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, last_y + last_height, window_x,
                                   window_bottom);
      const double top_edge = window_top - indent_px(box.top.space);
      const double bottom_edge = window_bottom + indent_px(box.bottom.space);
      if (right > left && bottom_edge > top_edge)
        paint_borders(cr, left, top_edge, right, bottom_edge, box, zoom);
    }
    if (end <= offset || end >= count)
      break;
    offset = end;
  }
}

namespace {

struct TabFollow {
  int left = 0;
  int right = 0;
  int decimal_x = 0;
};

// The text after a tab character, on the same line, up to the next tab.
TabFollow tab_follow(Gtk::TextView& view, Gtk::TextIter iter, int tab_right)
{
  TabFollow out;
  out.left = tab_right;
  out.right = tab_right;
  out.decimal_x = tab_right;
  if (iter.is_end() || !iter.forward_char())
    return out;
  bool any = false;
  bool decimal = false;
  int line_y = -1;
  while (!iter.is_end()) {
    const gunichar ch = iter.get_char();
    if (ch == '\t' || ch == '\n' || ch == 0)
      break;
    Gdk::Rectangle rect;
    view.get_iter_location(iter, rect);
    if (line_y < 0)
      line_y = rect.get_y();
    else if (rect.get_y() != line_y)
      break;
    if (!any)
      out.left = rect.get_x();
    any = true;
    out.right = rect.get_x() + rect.get_width();
    if (!decimal && ch == '.') {
      decimal = true;
      out.decimal_x = rect.get_x();
    }
    if (!iter.forward_char())
      break;
  }
  if (!any) {
    out.left = tab_right;
    out.right = tab_right;
    out.decimal_x = tab_right;
  } else if (!decimal) {
    out.decimal_x = out.right;
  }
  return out;
}

template <typename StopX>
const TabStop* leader_for(const std::vector<TabStop>& stops, int tab_right, const TabFollow& follow,
                          StopX stop_x)
{
  const TabStop* match = nullptr;
  int best = 0;
  bool found = false;
  for (const TabStop& stop : stops) {
    if (stop.align == TabAlign::Bar || stop.leader == TabLeader::None)
      continue;
    const int at = tab_align_x(stop.align, tab_right, follow.left, follow.right, follow.decimal_x);
    const int gap = std::abs(stop_x(stop.position) - at);
    if (gap <= 3 && (!found || gap <= best)) {
      best = gap;
      match = &stop;
      found = true;
    }
  }
  return match;
}

void paint_leader(const Cairo::RefPtr<Cairo::Context>& cr, TabLeader leader, double x0, double x1,
                  double baseline)
{
  if (x1 <= x0 + 1)
    return;
  if (leader == TabLeader::Underline) {
    cr->set_line_width(1);
    cr->move_to(x0, baseline + 0.5);
    cr->line_to(x1, baseline + 0.5);
    cr->stroke();
    return;
  }
  const double step = leader == TabLeader::Hyphen ? 6 : 4;
  const double mark = leader == TabLeader::Hyphen ? 3 : 1;
  for (double x = x0 + 1; x + mark <= x1; x += step) {
    cr->rectangle(x, baseline - 0.5, mark, 1);
    cr->fill();
  }
}

}  // namespace

void MainWindow::paint_story_tabs(Gtk::TextView& view, const Cairo::RefPtr<Cairo::Context>& cr,
                                  const Paragraph& paragraph, int offset, int len)
{
  const std::vector<TabStop> stops = clamp_tabs(paragraph.tabs);
  if (stops.empty() || len <= 0)
    return;
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  const int count = buffer->get_char_count();
  if (offset < 0 || offset >= count)
    return;
  const int end = std::min(count, offset + len);
  const Indents indents = clamp_indents(paragraph.indents);
  const bool list = paragraph.list.kind != ListKind::None;
  const int origin =
      list ? std::max(0, indents.left) : std::max(0, indents.left + std::min(0, indents.first));
  const double zoom = story_zoom();
  auto px = [zoom](int twips) { return twips_to_px(twips, zoom); };
  const int layout_left = view.get_left_margin() + px(origin);
  auto stop_x = [&](int position) { return layout_left + px(position) - px(origin); };
  auto first = buffer->get_iter_at_offset(offset);
  auto last = buffer->get_iter_at_offset(end > offset ? end - 1 : offset);
  int y = 0;
  int height = 0;
  int last_y = 0;
  int last_height = 0;
  view.get_line_yrange(first, y, height);
  view.get_line_yrange(last, last_y, last_height);
  const Gdk::RGBA color = view.get_style_context()->get_color(view.get_state_flags());
  cr->save();
  cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(), color.get_alpha());
  for (const TabStop& stop : stops) {
    if (stop.align != TabAlign::Bar)
      continue;
    int wx = 0;
    int top = 0;
    int bottom = 0;
    view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, stop_x(stop.position), y, wx, top);
    view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, last_y + last_height, wx, bottom);
    cr->set_line_width(1);
    cr->move_to(wx + 0.5, top);
    cr->line_to(wx + 0.5, bottom);
    cr->stroke();
  }
  for (auto iter = buffer->get_iter_at_offset(offset); !iter.is_end() && iter.get_offset() < end;
       ++iter) {
    if (iter.get_char() != '\t')
      continue;
    Gdk::Rectangle rect;
    view.get_iter_location(iter, rect);
    const int right = rect.get_x() + rect.get_width();
    const TabFollow follow = tab_follow(view, iter, right);
    const TabStop* match = leader_for(stops, right, follow, stop_x);
    if (!match)
      continue;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, rect.get_x(),
                                 rect.get_y() + rect.get_height() - 2, x0, y0);
    view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, right, 0, x1, y1);
    paint_leader(cr, match->leader, x0, x1, y0);
  }
  cr->restore();
}

void MainWindow::paint_story_border(Gtk::TextView& view, const Cairo::RefPtr<Cairo::Context>& cr,
                                    const Paragraph& paragraph, int offset, int len)
{
  if (borders_plain(paragraph.borders) || len <= 0)
    return;
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  const int width = view.get_allocated_width();
  if (width <= 0)
    return;
  const int count = buffer->get_char_count();
  if (offset < 0 || offset >= count)
    return;
  const int end = std::min(count, offset + len);
  auto first = buffer->get_iter_at_offset(offset);
  auto last = buffer->get_iter_at_offset(end > offset ? end - 1 : offset);
  int y = 0;
  int height = 0;
  int last_y = 0;
  int last_height = 0;
  view.get_line_yrange(first, y, height);
  view.get_line_yrange(last, last_y, last_height);
  auto gap_of = [](const Gtk::TextIter& at, bool above) {
    int gap = 0;
    for (const auto& tag : at.get_tags()) {
      if (above && tag->property_pixels_above_lines_set())
        gap = std::max(gap, static_cast<int>(tag->property_pixels_above_lines()));
      else if (!above && tag->property_pixels_below_lines_set())
        gap = std::max(gap, static_cast<int>(tag->property_pixels_below_lines()));
    }
    return gap;
  };
  const Borders borders = clamp_borders(paragraph.borders);
  const int text_top = y + gap_of(first, true);
  const int text_bottom = last_y + last_height - gap_of(last, false);
  const double left =
      view.get_left_margin() + indent_px(paragraph.indents.left) - indent_px(borders.left.space);
  const double right = static_cast<double>(width) - view.get_right_margin() -
                       indent_px(paragraph.indents.right) + indent_px(borders.right.space);
  int window_x = 0;
  int window_top = 0;
  int window_bottom = 0;
  view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, text_top - indent_px(borders.top.space),
                               window_x, window_top);
  view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0,
                               text_bottom + indent_px(borders.bottom.space), window_x,
                               window_bottom);
  if (right > left && window_bottom > window_top)
    paint_borders(cr, left, window_top, right, window_bottom, borders, zoom_factor());
}

void MainWindow::draw_story_borders(Gtk::TextView& view, const std::vector<Paragraph>& paragraphs,
                                    const Cairo::RefPtr<Cairo::Context>& cr)
{
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  const int count = buffer->get_char_count();
  int offset = 0;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    const Paragraph& paragraph = paragraphs[i];
    const int len = static_cast<int>(Glib::ustring(paragraph_plain(paragraph)).length());
    if (offset < count) {
      paint_story_border(view, cr, paragraph, offset, std::min(len, count - offset));
      paint_story_tabs(view, cr, paragraph, offset, std::min(len, count - offset));
    }
    offset += len;
    if (i + 1 < paragraphs.size())
      offset += 1;
  }
}

void MainWindow::draw_note_borders(Gtk::TextView& view,
                                   const std::vector<std::vector<Paragraph>>& notes,
                                   const Cairo::RefPtr<Cairo::Context>& cr)
{
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  const int count = buffer->get_char_count();
  int offset = 0;
  for (size_t n = 0; n < notes.size(); ++n) {
    if (n)
      offset += 2;
    const std::vector<Paragraph>& paragraphs = notes[n];
    for (size_t i = 0; i < paragraphs.size(); ++i) {
      const Paragraph& paragraph = paragraphs[i];
      const int len = static_cast<int>(Glib::ustring(paragraph_plain(paragraph)).length());
      if (offset < count) {
        paint_story_border(view, cr, paragraph, offset, std::min(len, count - offset));
        paint_story_tabs(view, cr, paragraph, offset, std::min(len, count - offset));
      }
      offset += len;
      if (i + 1 < paragraphs.size())
        offset += 1;
    }
  }
}

void MainWindow::draw_tab_marks(Gtk::TextView& view, const Cairo::RefPtr<Cairo::Context>& cr,
                                bool cell)
{
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  bool any = false;
  const char* prefix = cell ? "cparat" : "parat";
  const size_t nprefix = cell ? 6 : 5;
  buffer->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (any)
      return;
    const std::string name = tag_name(tag);
    if (name.size() >= nprefix && name.compare(0, nprefix, prefix) == 0)
      any = true;
  });
  if (!any)
    return;
  Gdk::Rectangle visible;
  view.get_visible_rect(visible);
  double clip_x1 = 0;
  double clip_y1 = 0;
  double clip_x2 = 0;
  double clip_y2 = 0;
  cr->get_clip_extents(clip_x1, clip_y1, clip_x2, clip_y2);
  int ignored = 0;
  int clip_top = 0;
  int clip_bottom = 0;
  view.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, static_cast<int>(std::floor(clip_y1)),
                               ignored, clip_top);
  view.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, static_cast<int>(std::ceil(clip_y2)),
                               ignored, clip_bottom);
  const int view_top = std::max(visible.get_y(), clip_top);
  const int view_bottom = std::min(visible.get_y() + visible.get_height(), clip_bottom);
  if (view_bottom < view_top)
    return;
  const int count = buffer->get_char_count();
  int offset = 0;
  if (!cell && count > 0) {
    Gtk::TextIter top;
    int top_y = 0;
    view.get_line_at_y(top, std::max(0, view_top), top_y);
    offset = paragraph_start(top.get_offset());
  }
  const Gdk::RGBA color = view.get_style_context()->get_color(view.get_state_flags());
  cr->save();
  cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(), color.get_alpha());
  for (; offset < count;) {
    int end = offset;
    auto cursor = buffer->get_iter_at_offset(std::min(offset, count));
    while (!cursor.is_end()) {
      const bool newline = cursor.get_char() == '\n';
      cursor.forward_char();
      end = cursor.get_offset();
      if (newline)
        break;
    }
    if (end <= offset)
      break;
    std::vector<TabStop> stops;
    Indents indents;
    bool list = false;
    bool found = false;
    auto at = buffer->get_iter_at_offset(offset);
    if (cell) {
      Align align = Align::Left;
      Spacing spacing;
      int basis = 22;
      int heading = 0;
      Borders borders;
      Shading shading;
      for (const auto& tag : at.get_tags()) {
        if (parse_cpara(tag_name(tag), indents, align, spacing, basis, heading, borders, shading,
                        stops)) {
          found = true;
          break;
        }
      }
    } else {
      ParaFormat format;
      if (auto tag = para_tag_at(at)) {
        if (parse_para(tag_name(tag), format)) {
          stops = format.tabs;
          indents = format.indents;
          list = format.list.kind != ListKind::None;
          found = true;
        }
      }
    }
    int y = 0;
    int height = 0;
    view.get_line_yrange(at, y, height);
    if (y > view_bottom)
      break;
    if (found && !stops.empty() && y + height >= view_top) {
      auto last = buffer->get_iter_at_offset(end > offset ? end - 1 : offset);
      int last_y = 0;
      int last_height = 0;
      view.get_line_yrange(last, last_y, last_height);
      const int origin = !cell && list ? std::max(0, indents.left)
                                       : std::max(0, indents.left + std::min(0, indents.first));
      int layout_left = margin_left();
      if (cell)
        layout_left =
            indents == Indents{} ? view.get_left_margin() : std::max(0, indent_px(origin));
      else if (list || !(indents == Indents{}))
        layout_left = margin_left() + indent_px(origin);
      auto stop_x = [&](int position) {
        return layout_left + indent_px(position) - indent_px(origin);
      };
      for (const TabStop& stop : stops) {
        if (stop.align != TabAlign::Bar)
          continue;
        int wx = 0;
        int top = 0;
        int bottom = 0;
        view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, stop_x(stop.position), y, wx, top);
        view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, last_y + last_height, wx, bottom);
        cr->set_line_width(1);
        cr->move_to(wx + 0.5, top);
        cr->line_to(wx + 0.5, bottom);
        cr->stroke();
      }
      for (auto iter = buffer->get_iter_at_offset(offset);
           !iter.is_end() && iter.get_offset() < end; ++iter) {
        if (iter.get_char() != '\t')
          continue;
        Gdk::Rectangle rect;
        view.get_iter_location(iter, rect);
        const int right = rect.get_x() + rect.get_width();
        const TabFollow follow = tab_follow(view, iter, right);
        const TabStop* match = leader_for(stops, right, follow, stop_x);
        if (!match)
          continue;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int ignored_y = 0;
        view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, rect.get_x(),
                                     rect.get_y() + std::max(0, rect.get_height() - 2), x0, y0);
        view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, right, 0, x1, ignored_y);
        paint_leader(cr, match->leader, x0, x1, y0);
      }
    }
    offset = end;
  }
  cr->restore();
}

bool MainWindow::on_text_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
  // List labels are drawn, not typed. They are not in the buffer, so find,
  // copy, and the file never see them. Each hangs in its paragraph's
  // first-line indent, in the font of the paragraph's first character.
  if (!buffer_)
    return false;
  // The gray between sheets. Painted after the text, so the text view's
  // white does not cover it. Nothing is typed in that band.
  draw_page_seams(cr);
  // A border-only document still draws. Lists are painted after this.
  draw_paragraph_borders(cr);
  draw_tab_marks(text_, cr, false);
  if (!any_list())
    return false;
  const std::vector<ListLine>& lines = list_lines();
  if (!list_lines_any_)
    return false;
  // A line's geometry holds while the text keeps its width, zoom and margin.
  const int text_width = text_.get_allocated_width();
  const int margin = margin_left();
  const double zoom = zoom_factor();
  if (text_width != label_geometry_width_ || margin != label_geometry_margin_ ||
      zoom != label_geometry_zoom_) {
    label_geometry_.clear();
    label_geometry_width_ = text_width;
    label_geometry_margin_ = margin;
    label_geometry_zoom_ = zoom;
  }
  // The text view is the whole page inside the pasteboard's scroller, so its
  // visible rectangle is the whole text. What is drawn now is the clip: the
  // part of the page in view, or less.
  Gdk::Rectangle visible;
  text_.get_visible_rect(visible);
  double clip_x1 = 0;
  double clip_y1 = 0;
  double clip_x2 = 0;
  double clip_y2 = 0;
  cr->get_clip_extents(clip_x1, clip_y1, clip_x2, clip_y2);
  int ignored = 0;
  int clip_top = 0;
  int clip_bottom = 0;
  text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, static_cast<int>(std::floor(clip_y1)),
                                ignored, clip_top);
  text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, static_cast<int>(std::ceil(clip_y2)),
                                ignored, clip_bottom);
  const int view_top = std::max(visible.get_y(), clip_top);
  const int view_bottom = std::min(visible.get_y() + visible.get_height(), clip_bottom);
  if (view_bottom < view_top)
    return false;
  const Gdk::RGBA color = text_.get_style_context()->get_color(text_.get_state_flags());
  // Only the items in view are measured and drawn. Paragraphs go down the
  // page in order: start one before the last paragraph to begin at or above
  // the view's top line, and stop at the first one below its bottom.
  Gtk::TextIter top;
  int top_y = 0;
  text_.get_line_at_y(top, view_top, top_y);
  const int top_line = top.get_line();
  size_t first = static_cast<size_t>(
      std::upper_bound(lines.begin(), lines.end(), top_line,
                       [](int line, const ListLine& item) { return line < item.line; }) -
      lines.begin());
  first = first >= 2 ? first - 2 : 0;
  for (size_t i = first; i < lines.size(); ++i) {
    const ParaFormat& format = lines[i].format;
    if (format.list.kind == ListKind::None)
      continue;
    const auto start = buffer_->get_iter_at_line(lines[i].line);
    int line_y = 0;
    int line_height = 0;
    text_.get_line_yrange(start, line_y, line_height);
    if (line_y + line_height < view_top)
      continue;
    Gdk::Rectangle where;
    label_where(start, line_y, line_height, where);
    if (where.get_y() + where.get_height() < view_top)
      continue;
    const int offset = start.get_offset();
    Glib::RefPtr<Pango::Layout> layout;
    int x = 0;
    if (!list_label_at(label_paragraph(i, offset), offset, lines[i].number, layout, x, where))
      continue;
    int width = 0;
    int height = 0;
    layout->get_pixel_size(width, height);
    // A label sits on its line's bottom, so one taller than its line reaches
    // above the line's top. The first label wholly below the view ends it.
    if (std::min(where.get_y(), where.get_y() + where.get_height() - height) > view_bottom)
      break;
    int wx = 0;
    int wy = 0;
    text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, x, where.get_y(), wx, wy);
    cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(), color.get_alpha());
    cr->move_to(wx, wy + where.get_height() - height);
    layout->show_in_cairo_context(cr);
  }
  return false;
}

void MainWindow::sync_list_controls()
{
  // The toolbar's Bullets and Numbering show the caret's paragraph.
  if (!buffer_)
    return;
  const ListKind kind = clamp_list(focused_para().list).kind;
  const bool was = suppress_format_;
  suppress_format_ = true;
  if (bullets_toggle_ && bullets_toggle_->get_active() != (kind == ListKind::Bullet))
    bullets_toggle_->set_active(kind == ListKind::Bullet);
  if (numbering_toggle_ && numbering_toggle_->get_active() != (kind == ListKind::Number))
    numbering_toggle_->set_active(kind == ListKind::Number);
  suppress_format_ = was;
}

void MainWindow::apply_focused_tabs(std::vector<TabStop> tabs)
{
  tabs = clamp_tabs(std::move(tabs));
  if (tabs == focused_para().tabs)
    return;
  apply_para_edit([&tabs](ParaFormat& format) { format.tabs = tabs; });
  ruler_.queue_draw();
}

void MainWindow::on_tabs()
{
  // Word 97's Format > Tabs. The list is the paragraph's own stops. The
  // default interval is the document's, stored as \deftab when it is not
  // half an inch. Set is what changes a stop; OK applies the list.
  const Units units = settings_.units;
  const int opened_default = clamp_default_tab(default_tab_);
  std::vector<TabStop> stops = clamp_tabs(focused_para().tabs);
  const double step = units_step(units);
  const double max_value = max_measure(units);

  Gtk::Dialog dialog("Tabs", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  dialog.get_content_area()->set_margin_top(12);
  dialog.get_content_area()->set_margin_bottom(12);
  dialog.get_content_area()->set_margin_start(12);
  dialog.get_content_area()->set_margin_end(12);
  dialog.get_content_area()->set_spacing(12);

  auto spin = [units, step, max_value](int twips) {
    auto* button =
        Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(0, 0, max_value, step, step * 5),
                                        step, static_cast<guint>(units_digits(units))));
    button->set_numeric(false);
    button->set_update_policy(Gtk::UPDATE_IF_VALID);
    button->set_width_chars(8);
    struct Typed {
      bool bad = false;
      double kept = 0;
    };
    auto typed = std::make_shared<Typed>();
    button->signal_output().connect(
        [button, units, typed] {
          if (typed->bad && button->get_value() == typed->kept)
            return true;
          typed->bad = false;
          button->set_text(format_measure(button->get_value(), units));
          return true;
        },
        false);
    button->signal_input().connect(
        [button, units, typed](double* value) {
          double parsed = 0;
          if (check_measure(button->get_text().raw(), units, parsed) == MeasureCheck::Ok) {
            typed->bad = false;
            *value = parsed;
            return 1;
          }
          typed->bad = true;
          typed->kept = button->get_value();
          *value = typed->kept;
          return 1;
        },
        true);
    button->signal_focus_out_event().connect(
        [button, units, typed](GdkEventFocus*) {
          button->update();
          if (!typed->bad)
            button->set_text(format_measure(button->get_value(), units));
          return false;
        },
        false);
    button->set_value(units_round(twips_to_units(twips, units), units));
    return button;
  };
  auto label = [](const char* text, Gtk::Widget& target) {
    auto* item = Gtk::manage(new Gtk::Label(text, true));
    item->set_halign(Gtk::ALIGN_START);
    item->set_mnemonic_widget(target);
    return item;
  };

  auto* position = spin(stops.empty() ? 0 : stops.front().position);
  position->set_activates_default(false);
  auto* list_label = Gtk::manage(new Gtk::Label("Tab stops:"));
  list_label->set_halign(Gtk::ALIGN_START);

  struct StopColumns : public Gtk::TreeModelColumnRecord {
    StopColumns()
    {
      add(position);
      add(text);
    }
    Gtk::TreeModelColumn<int> position;
    Gtk::TreeModelColumn<Glib::ustring> text;
  };
  static StopColumns columns;
  auto store = Gtk::ListStore::create(columns);
  auto* list = Gtk::manage(new Gtk::TreeView(store));
  list->append_column("Position", columns.text);
  list->set_headers_visible(false);
  list->set_enable_search(false);
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_shadow_type(Gtk::SHADOW_IN);
  scroll->set_size_request(160, 140);
  scroll->add(*list);

  auto* set_button = Gtk::manage(new Gtk::Button("_Set", true));
  auto* clear_button = Gtk::manage(new Gtk::Button("_Clear", true));
  auto* clear_all = Gtk::manage(new Gtk::Button("Clear _All", true));
  auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
  buttons->set_valign(Gtk::ALIGN_START);
  buttons->pack_start(*set_button, Gtk::PACK_SHRINK);
  buttons->pack_start(*clear_button, Gtk::PACK_SHRINK);
  buttons->pack_start(*clear_all, Gtk::PACK_SHRINK);

  auto* top = Gtk::manage(new Gtk::Grid());
  top->set_column_spacing(12);
  top->set_row_spacing(6);
  top->attach(*label("_Tab stop position:", *position), 0, 0, 1, 1);
  top->attach(*position, 1, 0, 1, 1);
  top->attach(*list_label, 0, 1, 2, 1);
  top->attach(*scroll, 0, 2, 2, 1);
  top->attach(*buttons, 2, 2, 1, 1);

  auto* left_align = Gtk::manage(new Gtk::RadioButton("_Left", true));
  auto align_group = left_align->get_group();
  auto* center_align = Gtk::manage(new Gtk::RadioButton(align_group, "C_enter", true));
  auto* right_align = Gtk::manage(new Gtk::RadioButton(align_group, "_Right", true));
  auto* decimal_align = Gtk::manage(new Gtk::RadioButton(align_group, "Deci_mal", true));
  auto* bar_align = Gtk::manage(new Gtk::RadioButton(align_group, "_Bar", true));
  std::array<Gtk::RadioButton*, 5> aligns = {left_align, center_align, right_align, decimal_align,
                                             bar_align};
  auto* align_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  for (Gtk::RadioButton* radio : aligns)
    align_box->pack_start(*radio, Gtk::PACK_SHRINK);
  auto* align_frame = Gtk::manage(new Gtk::Frame("Alignment"));
  align_frame->add(*align_box);
  align_box->set_margin_top(4);
  align_box->set_margin_bottom(6);
  align_box->set_margin_start(8);
  align_box->set_margin_end(8);

  auto* none_leader = Gtk::manage(new Gtk::RadioButton("_None", true));
  auto leader_group = none_leader->get_group();
  auto* dot_leader = Gtk::manage(new Gtk::RadioButton(leader_group, "D_ots", true));
  auto* dash_leader = Gtk::manage(new Gtk::RadioButton(leader_group, "Das_hes", true));
  auto* under_leader = Gtk::manage(new Gtk::RadioButton(leader_group, "_Underline", true));
  std::array<Gtk::RadioButton*, 4> leaders = {none_leader, dot_leader, dash_leader, under_leader};
  auto* leader_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  for (Gtk::RadioButton* radio : leaders)
    leader_box->pack_start(*radio, Gtk::PACK_SHRINK);
  auto* leader_frame = Gtk::manage(new Gtk::Frame("Leader"));
  leader_frame->add(*leader_box);
  leader_box->set_margin_top(4);
  leader_box->set_margin_bottom(6);
  leader_box->set_margin_start(8);
  leader_box->set_margin_end(8);

  auto* choices = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  choices->pack_start(*align_frame, Gtk::PACK_SHRINK);
  choices->pack_start(*leader_frame, Gtk::PACK_SHRINK);

  auto* def_spin = spin(opened_default);
  def_spin->set_activates_default(true);
  auto* def_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  def_row->pack_start(*label("_Default tab stops:", *def_spin), Gtk::PACK_SHRINK);
  def_row->pack_start(*def_spin, Gtk::PACK_SHRINK);

  auto* notice = Gtk::manage(new Gtk::Label());
  notice->set_line_wrap(true);
  notice->set_max_width_chars(48);
  notice->set_halign(Gtk::ALIGN_START);
  notice->set_no_show_all(true);
  notice->set_visible(false);

  dialog.get_content_area()->pack_start(*top, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*choices, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*def_row, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*notice, Gtk::PACK_SHRINK);

  auto sync_leaders = [bar_align, leaders] {
    const bool bar = bar_align->get_active();
    for (Gtk::RadioButton* radio : leaders)
      radio->set_sensitive(!bar);
  };
  bar_align->signal_toggled().connect(sync_leaders);

  auto chosen_align = [aligns] {
    if (aligns[1]->get_active())
      return TabAlign::Center;
    if (aligns[2]->get_active())
      return TabAlign::Right;
    if (aligns[3]->get_active())
      return TabAlign::Decimal;
    if (aligns[4]->get_active())
      return TabAlign::Bar;
    return TabAlign::Left;
  };
  auto chosen_leader = [leaders] {
    if (leaders[1]->get_active())
      return TabLeader::Dot;
    if (leaders[2]->get_active())
      return TabLeader::Hyphen;
    if (leaders[3]->get_active())
      return TabLeader::Underline;
    return TabLeader::None;
  };
  auto show_stop = [&](const TabStop& stop) {
    position->set_value(units_round(twips_to_units(stop.position, units), units));
    int align_at = 0;
    if (stop.align == TabAlign::Center)
      align_at = 1;
    else if (stop.align == TabAlign::Right)
      align_at = 2;
    else if (stop.align == TabAlign::Decimal)
      align_at = 3;
    else if (stop.align == TabAlign::Bar)
      align_at = 4;
    aligns[static_cast<size_t>(align_at)]->set_active(true);
    int leader_at = 0;
    if (stop.leader == TabLeader::Dot)
      leader_at = 1;
    else if (stop.leader == TabLeader::Hyphen)
      leader_at = 2;
    else if (stop.leader == TabLeader::Underline)
      leader_at = 3;
    leaders[static_cast<size_t>(leader_at)]->set_active(true);
    sync_leaders();
  };

  bool filling = false;
  auto refill = [&](bool select, int select_position) {
    filling = true;
    store->clear();
    int row = 0;
    int chosen = -1;
    for (const TabStop& stop : stops) {
      auto iter = store->append();
      (*iter)[columns.position] = stop.position;
      (*iter)[columns.text] = format_measure(twips_to_units(stop.position, units), units);
      if (select && stop.position == select_position)
        chosen = row;
      ++row;
    }
    filling = false;
    if (chosen >= 0) {
      Gtk::TreePath path;
      path.push_back(chosen);
      list->get_selection()->select(path);
      if (list->get_realized())
        list->scroll_to_row(path);
    }
  };
  list->get_selection()->signal_changed().connect([&] {
    if (filling)
      return;
    auto iter = list->get_selection()->get_selected();
    if (!iter)
      return;
    const int at = (*iter)[columns.position];
    for (const TabStop& stop : stops) {
      if (stop.position == at) {
        show_stop(stop);
        return;
      }
    }
  });

  auto set_stop = [&] {
    double parsed = 0;
    const MeasureCheck check = check_measure(position->get_text().raw(), units, parsed);
    if (check != MeasureCheck::Ok) {
      notice->set_text(measure_message(check, units));
      notice->set_visible(true);
      return;
    }
    std::vector<int> positions;
    positions.reserve(stops.size());
    for (const TabStop& stop : stops)
      positions.push_back(stop.position);
    int twips = shown_stop_twips(parsed, positions, units);
    twips = std::max(0, std::min(kMaxIndent, twips));
    bool exists = false;
    for (const TabStop& stop : stops) {
      if (stop.position == twips)
        exists = true;
    }
    if (!exists && static_cast<int>(stops.size()) >= kMaxTabStops) {
      notice->set_text("A paragraph holds at most 50 tab stops.");
      notice->set_visible(true);
      return;
    }
    TabStop stop;
    stop.position = twips;
    stop.align = chosen_align();
    stop.leader = stop.align == TabAlign::Bar ? TabLeader::None : chosen_leader();
    stops.push_back(stop);
    stops = clamp_tabs(std::move(stops));
    notice->set_visible(false);
    refill(true, twips);
  };
  auto clear_one = [&] {
    auto iter = list->get_selection()->get_selected();
    if (!iter)
      return;
    const int selected = (*iter)[columns.position];
    stops.erase(
        std::remove_if(stops.begin(), stops.end(),
                       [selected](const TabStop& stop) { return stop.position == selected; }),
        stops.end());
    notice->set_visible(false);
    refill(false, 0);
  };
  auto clear_every = [&] {
    stops.clear();
    notice->set_visible(false);
    refill(false, 0);
  };
  set_button->signal_clicked().connect(set_stop);
  clear_button->signal_clicked().connect(clear_one);
  clear_all->signal_clicked().connect(clear_every);
  position->signal_activate().connect(set_stop);

  dialog.show_all_children();
  refill(!stops.empty(), stops.empty() ? 0 : stops.front().position);
  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      restore_edit_focus();
      return;
    }
    double parsed = 0;
    const MeasureCheck check = check_measure(def_spin->get_text().raw(), units, parsed);
    if (check != MeasureCheck::Ok) {
      notice->set_text(measure_message(check, units));
      notice->set_visible(true);
      continue;
    }
    const double shown = units_round(twips_to_units(opened_default, units), units);
    const int interval = clamp_default_tab(keep_twips(opened_default, shown, parsed, units));
    const bool tabs_changed = stops != focused_para().tabs;
    const bool interval_changed = interval != clamp_default_tab(default_tab_);
    if (tabs_changed)
      apply_focused_tabs(stops);
    if (interval_changed) {
      const Document before = capture();
      Document after = before;
      after.default_tab = interval;
      if (!(before == after))
        commit_document(before, after);
    }
    restore_edit_focus();
    ruler_.queue_draw();
    return;
  }
}

void MainWindow::on_paragraph()
{
  // Word 97's Format > Paragraph: alignment, indentation, and spacing, in
  // the unit Tools > Options... chose.
  const Units units = settings_.units;
  const ParaFormat current_para = focused_para();
  const int caret = cursor_offset();
  const Indents current = current_para.indents;
  const Align current_align = current_para.align;
  const Spacing current_spacing = current_para.spacing;
  const double font_pt = std::min(1584.0, size_from_half_points(basis_half_at(caret)));
  Gtk::Dialog dialog("Paragraph", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  auto* frame = Gtk::manage(new Gtk::Frame("Indentation"));
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(8);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  frame->set_margin_top(12);
  frame->set_margin_bottom(12);
  frame->set_margin_start(12);
  frame->set_margin_end(12);
  const double step = units_step(units);
  const double max_value = max_measure(units);
  // Each field shows its value with the unit's suffix, 0.5" or 1.27 cm, and
  // reads back a bare number, a suffixed one, or the other unit typed out.
  // Text that is not a measure, or is out of range, stays in the field as
  // typed: OK names the range and selects it (check_paragraph), as Word 97
  // does, where GTK would quietly put the old value back.
  auto spin = [units, step, max_value](int twips) {
    auto* button =
        Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(0, 0, max_value, step, step * 5),
                                        step, static_cast<guint>(units_digits(units))));
    button->set_numeric(false);
    button->set_update_policy(Gtk::UPDATE_IF_VALID);
    button->set_width_chars(8);
    // Set while the text is not a measure in range; `kept` is the value the
    // field had then. A new value (the arrows, Special's default) ends it.
    struct Typed {
      bool bad = false;
      double kept = 0;
    };
    auto typed = std::make_shared<Typed>();
    button->signal_output().connect(
        [button, units, typed] {
          if (typed->bad && button->get_value() == typed->kept)
            return true;
          typed->bad = false;
          button->set_text(format_measure(button->get_value(), units));
          return true;
        },
        false);
    // "input" has no accumulator: the last handler's answer wins, and the
    // class handler answers "not handled". Connect after it. Text that will
    // not do keeps the old value without being written over.
    button->signal_input().connect(
        [button, units, typed](double* value) {
          double parsed = 0;
          if (check_measure(button->get_text().raw(), units, parsed) == MeasureCheck::Ok) {
            typed->bad = false;
            *value = parsed;
            return 1;
          }
          typed->bad = true;
          typed->kept = button->get_value();
          *value = typed->kept;
          return 1;
        },
        true);
    // Leaving a field reads what was typed and shows it back in the unit, so
    // "1 in" becomes 2.54 cm. Text that will not do is left for OK.
    button->signal_focus_out_event().connect(
        [button, units, typed](GdkEventFocus*) {
          button->update();
          if (!typed->bad)
            button->set_text(format_measure(button->get_value(), units));
          return false;
        },
        false);
    button->set_value(units_round(twips_to_units(twips, units), units));
    button->set_activates_default(true);
    return button;
  };
  auto label = [](const char* text, Gtk::Widget& target) {
    auto* item = Gtk::manage(new Gtk::Label(text, true));
    item->set_halign(Gtk::ALIGN_START);
    item->set_mnemonic_widget(target);
    return item;
  };
  const int magnitude = current.first < 0 ? -current.first : current.first;
  auto* left = spin(current.left);
  auto* right = spin(current.right);
  auto* special = Gtk::manage(new Gtk::ComboBoxText());
  special->append("(none)");
  special->append("First line");
  special->append("Hanging");
  const int special_was = current.first > 0 ? 1 : current.first < 0 ? 2 : 0;
  special->set_active(special_was);
  auto* by = spin(magnitude);
  by->set_sensitive(current.first != 0);
  // Word fills in half an inch, or 1.27 cm, when Special turns on at zero.
  const double default_by = units == Units::Centimetres ? 1.27 : 0.5;
  special->signal_changed().connect([special, by, default_by] {
    by->set_sensitive(special->get_active_row_number() != 0);
    if (special->get_active_row_number() != 0 && by->get_value() == 0)
      by->set_value(default_by);
  });
  // What each field showed when the dialog opened, to tell an untouched
  // field from an edited one.
  const double left_shown = left->get_value();
  const double right_shown = right->get_value();
  const double by_shown = by->get_value();
  grid->attach(*label("_Left:", *left), 0, 0, 1, 1);
  grid->attach(*left, 1, 0, 1, 1);
  grid->attach(*label("_Right:", *right), 0, 1, 1, 1);
  grid->attach(*right, 1, 1, 1, 1);
  grid->attach(*label("_Special:", *special), 2, 0, 1, 1);
  grid->attach(*special, 2, 1, 1, 1);
  grid->attach(*label("B_y:", *by), 3, 0, 1, 1);
  grid->attach(*by, 3, 1, 1, 1);
  // Alignment sits above Indentation, as in Word 97's dialog.
  auto* alignment = Gtk::manage(new Gtk::ComboBoxText());
  alignment->append("Left");
  alignment->append("Centered");
  alignment->append("Right");
  alignment->append("Justified");
  alignment->set_active(static_cast<int>(current_align));
  auto* align_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  align_row->set_margin_top(12);
  align_row->set_margin_start(12);
  align_row->set_margin_end(12);
  align_row->pack_start(*label("Ali_gnment:", *alignment), Gtk::PACK_SHRINK);
  align_row->pack_start(*alignment, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*align_row, Gtk::PACK_SHRINK);

  frame->add(*grid);
  dialog.get_content_area()->pack_start(*frame, Gtk::PACK_SHRINK);

  // Spacing. Before and after use the Options unit. At least and Exactly
  // take a point size. Multiple takes a number of lines. Single, 1.5 and
  // Double leave At alone.
  auto* spacing_frame = Gtk::manage(new Gtk::Frame("Spacing"));
  auto* sgrid = Gtk::manage(new Gtk::Grid());
  sgrid->set_row_spacing(8);
  sgrid->set_column_spacing(12);
  sgrid->set_margin_top(8);
  sgrid->set_margin_bottom(12);
  sgrid->set_margin_start(12);
  sgrid->set_margin_end(12);
  spacing_frame->set_margin_bottom(12);
  spacing_frame->set_margin_start(12);
  spacing_frame->set_margin_end(12);
  auto* before = spin(current_spacing.before);
  auto* after = spin(current_spacing.after);
  const double before_shown = before->get_value();
  const double after_shown = after->get_value();
  auto* line = Gtk::manage(new Gtk::ComboBoxText());
  line->append("Single");
  line->append("1.5 lines");
  line->append("Double");
  line->append("At least");
  line->append("Exactly");
  line->append("Multiple");
  const int initial_rule = static_cast<int>(current_spacing.rule);
  line->set_active(initial_rule);
  auto at_points = [](int row) {
    return row == static_cast<int>(LineRule::AtLeast) || row == static_cast<int>(LineRule::Exactly);
  };
  auto uses_at = [at_points](int row) {
    return at_points(row) || row == static_cast<int>(LineRule::Multiple);
  };
  double initial_at = font_pt;
  if (at_points(initial_rule))
    initial_at = current_spacing.value / 20.0;
  else if (initial_rule == static_cast<int>(LineRule::Multiple))
    initial_at = current_spacing.value / 240.0;
  auto* at = Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(0, 0, 1584, 1, 6), 1, 2));
  at->set_numeric(false);
  at->set_update_policy(Gtk::UPDATE_IF_VALID);
  at->set_width_chars(8);
  at->set_activates_default(true);
  struct AtTyped {
    bool bad = false;
    double kept = 0;
  };
  auto at_typed = std::make_shared<AtTyped>();
  auto format_at = [](double value, bool points) {
    const int scaled = static_cast<int>(std::lround(std::max(0.0, value) * 100.0));
    const int whole = scaled / 100;
    const int frac = scaled % 100;
    std::string text = std::to_string(whole);
    if (frac != 0) {
      text.push_back('.');
      text.push_back(static_cast<char>('0' + frac / 10));
      if (frac % 10 != 0)
        text.push_back(static_cast<char>('0' + frac % 10));
    }
    if (points)
      text += " pt";
    return text;
  };
  auto accept_at = [](const std::string& text, bool points, double& number) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
      ++i;
    const size_t from = i;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
      ++i;
    bool dot = false;
    size_t digits = 0;
    for (; i < text.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      if (std::isdigit(c))
        ++digits;
      else if (text[i] == '.' && !dot)
        dot = true;
      else
        break;
    }
    if (digits == 0)
      return false;
    try {
      number = std::stod(text.substr(from, i - from));
    } catch (const std::exception&) {
      return false;
    }
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
      ++i;
    std::string suffix = text.substr(i);
    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.back())))
      suffix.pop_back();
    for (char& c : suffix)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (points) {
      if (!suffix.empty() && suffix != "pt" && suffix != "pts" && suffix != "point" &&
          suffix != "points")
        return false;
      return number >= 0 && number <= 1584;
    }
    if (!suffix.empty() && suffix != "li" && suffix != "line" && suffix != "lines")
      return false;
    return number >= 0 && number <= 132;
  };
  at->signal_output().connect(
      [at, line, at_typed, format_at] {
        if (at_typed->bad && at->get_value() == at_typed->kept)
          return true;
        at_typed->bad = false;
        const bool points = line->get_active_row_number() != static_cast<int>(LineRule::Multiple);
        at->set_text(format_at(at->get_value(), points));
        return true;
      },
      false);
  at->signal_input().connect(
      [at, line, at_typed, accept_at](double* value) {
        double parsed = 0;
        const bool points = line->get_active_row_number() != static_cast<int>(LineRule::Multiple);
        if (accept_at(at->get_text().raw(), points, parsed)) {
          at_typed->bad = false;
          *value = parsed;
          return 1;
        }
        at_typed->bad = true;
        at_typed->kept = at->get_value();
        *value = at_typed->kept;
        return 1;
      },
      true);
  at->signal_focus_out_event().connect(
      [at, at_typed, line, format_at](GdkEventFocus*) {
        at->update();
        if (!at_typed->bad) {
          const bool points = line->get_active_row_number() != static_cast<int>(LineRule::Multiple);
          at->set_text(format_at(at->get_value(), points));
        }
        return false;
      },
      false);
  auto tune = [at, uses_at](int row) {
    at->set_sensitive(uses_at(row));
    auto adj = at->get_adjustment();
    if (row == static_cast<int>(LineRule::Multiple)) {
      adj->set_upper(132);
      adj->set_step_increment(0.5);
      adj->set_page_increment(1);
    } else {
      adj->set_upper(1584);
      adj->set_step_increment(1);
      adj->set_page_increment(6);
    }
  };
  at->get_adjustment()->set_upper(1584);
  at->set_value(initial_at);
  tune(initial_rule);
  const double at_shown = at->get_value();
  auto rule_was = std::make_shared<int>(initial_rule);
  line->signal_changed().connect([line, at, rule_was, font_pt, at_points, tune] {
    const int now = line->get_active_row_number();
    const int previous = *rule_was;
    // At least and Exactly share a point size. Coming from any other rule
    // starts at the paragraph's font size. Multiple starts at 3, as Word does.
    at->get_adjustment()->set_upper(1584);
    if (at_points(now) && !at_points(previous))
      at->set_value(font_pt);
    else if (now == static_cast<int>(LineRule::Multiple) &&
             previous != static_cast<int>(LineRule::Multiple))
      at->set_value(3);
    *rule_was = now;
    tune(now);
    at->update();
  });
  sgrid->attach(*label("Be_fore:", *before), 0, 0, 1, 1);
  sgrid->attach(*before, 1, 0, 1, 1);
  sgrid->attach(*label("Aft_er:", *after), 2, 0, 1, 1);
  sgrid->attach(*after, 3, 0, 1, 1);
  sgrid->attach(*label("Li_ne spacing:", *line), 0, 1, 1, 1);
  sgrid->attach(*line, 1, 1, 1, 1);
  sgrid->attach(*label("_At:", *at), 2, 1, 1, 1);
  sgrid->attach(*at, 3, 1, 1, 1);
  spacing_frame->add(*sgrid);
  dialog.get_content_area()->pack_start(*spacing_frame, Gtk::PACK_SHRINK);
  dialog.show_all_children();
  // As in Word 97, OK on a choice that will not do says why and goes back to
  // the dialog with the field to fix selected, rather than quietly changing
  // the value: a field out of range or not a measure, a hanging indent past
  // the left margin, or indents that leave too little room for text.
  Indents chosen;
  Spacing chosen_spacing;
  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      restore_edit_focus();
      return;
    }
    ParaFields fields;
    fields.units = units;
    fields.current = current;
    fields.left = left->get_text().raw();
    fields.right = right->get_text().raw();
    fields.by = by->get_text().raw();
    fields.left_shown = left_shown;
    fields.right_shown = right_shown;
    fields.by_shown = by_shown;
    fields.special = special->get_active_row_number();
    fields.special_was = special_was;
    const ParaCheck check = check_paragraph(fields);
    SpacingFields sfields;
    sfields.units = units;
    sfields.current = current_spacing;
    sfields.before = before->get_text().raw();
    sfields.after = after->get_text().raw();
    sfields.at = at->get_text().raw();
    sfields.before_shown = before_shown;
    sfields.after_shown = after_shown;
    sfields.at_shown = at_shown;
    int row = line->get_active_row_number();
    if (row < 0 || row > static_cast<int>(LineRule::Multiple))
      row = 0;
    sfields.rule = static_cast<LineRule>(row);
    const SpacingCheck scheck =
        check.field == ParaField::None ? check_spacing(sfields) : SpacingCheck{};
    if (check.field == ParaField::None && scheck.field == SpacingField::None) {
      chosen = check.indents;
      chosen_spacing = scheck.spacing;
      break;
    }
    const bool indent_bad = check.field != ParaField::None;
    Gtk::MessageDialog message(dialog, indent_bad ? check.message : scheck.message, false,
                               Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
    message.set_title("Paragraph");
    message.run();
    message.hide();
    Gtk::SpinButton* offending = by;
    if (indent_bad) {
      offending = check.field == ParaField::Left    ? left
                  : check.field == ParaField::Right ? right
                                                    : by;
    } else if (scheck.field == SpacingField::Before) {
      offending = before;
    } else if (scheck.field == SpacingField::After) {
      offending = after;
    } else {
      offending = at;
    }
    offending->grab_focus();
    offending->select_region(0, -1);
  }
  const auto align = static_cast<Align>(alignment->get_active_row_number());
  const bool align_changed = align != current_align;
  const bool indents_changed = !(chosen == current);
  const bool spacing_changed = !(chosen_spacing == current_spacing);
  // Only what changed is applied, so a new alignment keeps each selected
  // paragraph's own indents, and new indents keep each one's alignment.
  if (!indents_changed && !spacing_changed) {
    if (!align_changed) {
      restore_edit_focus();
      return;
    }
    apply_align(align);
    return;
  }
  apply_para_edit([chosen, align, align_changed, chosen_spacing, indents_changed,
                   spacing_changed](ParaFormat& format) {
    if (indents_changed)
      format.indents = chosen;
    if (align_changed)
      format.align = align;
    if (spacing_changed)
      format.spacing = chosen_spacing;
  });
}

void MainWindow::on_borders()
{
  // Word 97's Format > Borders and Shading. Apply to is the paragraph, or
  // the whole cell when one is being edited. Switching Apply to drops edits
  // to the other target. Cancel writes nothing.
  const bool have_cell = editing_cell();
  const ParaFormat paragraph = focused_para();
  Borders cell_borders;
  Shading cell_shading;
  bool cell_set = false;
  int cell_table = 0;
  if (have_cell) {
    if (TableSheet* sheet = editing_sheet()) {
      int row = 0;
      int column = 0;
      int caret = 0;
      int bound = 0;
      if (cell_place(*sheet, row, column, caret, bound)) {
        if (const Paragraph* form = cell_paragraph_at(row, column, std::min(caret, bound))) {
          cell_borders = form->cell.borders;
          cell_shading = form->cell.shading;
          cell_set = form->cell.borders_set;
          cell_table = form->cell.table;
        }
      }
    }
  }

  enum class Setting { None, Box, Shadow, ThreeD, Custom };
  auto recognize = [](const Borders& borders) {
    const Borders box = clamp_borders(borders);
    if (borders_plain(box))
      return Setting::None;
    const bool bare =
        !box.top.shadow && !box.left.shadow && !box.bottom.shadow && !box.right.shadow;
    const bool same_width = box.top.width == box.left.width && box.top.width == box.bottom.width &&
                            box.top.width == box.right.width;
    const bool same_color = box.top.color == box.left.color && box.top.color == box.bottom.color &&
                            box.top.color == box.right.color;
    if (bare && same_width && same_color && box.top.style == BorderStyle::Inset &&
        box.left.style == BorderStyle::Inset && box.bottom.style == BorderStyle::Outset &&
        box.right.style == BorderStyle::Outset)
      return Setting::ThreeD;
    if (box.top == box.left && box.top == box.bottom && box.top == box.right &&
        box.top.style != BorderStyle::None)
      return box.top.shadow ? Setting::Shadow : Setting::Box;
    return Setting::Custom;
  };

  Gtk::Dialog dialog("Borders and Shading", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);

  auto* book = Gtk::manage(new Gtk::Notebook());
  book->set_margin_top(12);
  book->set_margin_start(12);
  book->set_margin_end(12);
  auto* borders_page = Gtk::manage(new Gtk::Grid());
  borders_page->set_row_spacing(8);
  borders_page->set_column_spacing(12);
  borders_page->set_margin_top(12);
  borders_page->set_margin_bottom(12);
  borders_page->set_margin_start(12);
  borders_page->set_margin_end(12);
  auto* shade_page = Gtk::manage(new Gtk::Grid());
  shade_page->set_row_spacing(8);
  shade_page->set_column_spacing(12);
  shade_page->set_margin_top(12);
  shade_page->set_margin_bottom(12);
  shade_page->set_margin_start(12);
  shade_page->set_margin_end(12);

  auto* setting_combo = Gtk::manage(new Gtk::ComboBoxText());
  setting_combo->append("None");
  setting_combo->append("Box");
  setting_combo->append("Shadow");
  setting_combo->append("3-D");
  setting_combo->append("Custom");
  auto* style_combo = Gtk::manage(new Gtk::ComboBoxText());
  for (int style = static_cast<int>(BorderStyle::Single);
       style <= static_cast<int>(BorderStyle::Engrave); ++style)
    style_combo->append(border_style_name(static_cast<BorderStyle>(style)));
  auto* color_combo = Gtk::manage(new Gtk::ComboBoxText());
  auto* fill_combo = Gtk::manage(new Gtk::ComboBoxText());
  auto* pattern_color = Gtk::manage(new Gtk::ComboBoxText());
  for (Gtk::ComboBoxText* combo : {color_combo, fill_combo, pattern_color}) {
    combo->append(ink_name(Ink::Auto, false));
    for (int ink = 1; ink <= 16; ++ink)
      combo->append(ink_name(static_cast<Ink>(ink), false));
  }
  auto* width_combo = Gtk::manage(new Gtk::ComboBoxText());
  std::vector<int> width_values;
  auto width_label = [](int twips) {
    static const char* labels[] = {"1/4 pt",   "1/2 pt", "3/4 pt",   "1 pt", "1 1/2 pt",
                                   "2 1/4 pt", "3 pt",   "4 1/2 pt", "6 pt"};
    for (int i = 0; i < kBorderWidthCount; ++i) {
      if (kBorderWidths[i] == twips)
        return std::string(labels[i]);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f pt", static_cast<double>(twips) / 20.0);
    return std::string(buf);
  };
  for (int i = 0; i < kBorderWidthCount; ++i) {
    width_values.push_back(kBorderWidths[i]);
    width_combo->append(width_label(kBorderWidths[i]));
  }
  auto* pattern_combo = Gtk::manage(new Gtk::ComboBoxText());
  std::vector<int> pattern_values;
  auto pattern_label = [](int pattern) {
    for (int i = 0; i < kShadeStepCount; ++i) {
      if (kShadeSteps[i].pattern == pattern)
        return std::string(kShadeSteps[i].name);
    }
    if (pattern % 100 == 0)
      return std::to_string(pattern / 100) + "%";
    if (pattern % 100 == 50)
      return std::to_string(pattern / 100) + ".5%";
    return std::to_string(pattern);
  };
  for (int i = 0; i < kShadeStepCount; ++i) {
    pattern_values.push_back(kShadeSteps[i].pattern);
    pattern_combo->append(kShadeSteps[i].name);
  }
  auto* top_check = Gtk::manage(new Gtk::CheckButton("_Top", true));
  auto* left_check = Gtk::manage(new Gtk::CheckButton("_Left", true));
  auto* bottom_check = Gtk::manage(new Gtk::CheckButton("_Bottom", true));
  auto* right_check = Gtk::manage(new Gtk::CheckButton("_Right", true));
  auto* checks = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  checks->pack_start(*top_check, Gtk::PACK_SHRINK);
  checks->pack_start(*left_check, Gtk::PACK_SHRINK);
  checks->pack_start(*bottom_check, Gtk::PACK_SHRINK);
  checks->pack_start(*right_check, Gtk::PACK_SHRINK);

  auto label = [](const char* text, Gtk::Widget& target) {
    auto* item = Gtk::manage(new Gtk::Label(text, true));
    item->set_halign(Gtk::ALIGN_START);
    item->set_mnemonic_widget(target);
    return item;
  };
  borders_page->attach(*label("_Setting:", *setting_combo), 0, 0, 1, 1);
  borders_page->attach(*setting_combo, 1, 0, 1, 1);
  borders_page->attach(*label("St_yle:", *style_combo), 0, 1, 1, 1);
  borders_page->attach(*style_combo, 1, 1, 1, 1);
  borders_page->attach(*label("C_olor:", *color_combo), 0, 2, 1, 1);
  borders_page->attach(*color_combo, 1, 2, 1, 1);
  borders_page->attach(*label("_Width:", *width_combo), 0, 3, 1, 1);
  borders_page->attach(*width_combo, 1, 3, 1, 1);
  borders_page->attach(*checks, 0, 4, 2, 1);
  shade_page->attach(*label("_Fill:", *fill_combo), 0, 0, 1, 1);
  shade_page->attach(*fill_combo, 1, 0, 1, 1);
  shade_page->attach(*label("_Pattern:", *pattern_combo), 0, 1, 1, 1);
  shade_page->attach(*pattern_combo, 1, 1, 1, 1);
  shade_page->attach(*label("Pattern colo_ur:", *pattern_color), 0, 2, 1, 1);
  shade_page->attach(*pattern_color, 1, 2, 1, 1);
  book->append_page(*borders_page, "Borders");
  book->append_page(*shade_page, "Shading");

  auto* apply = Gtk::manage(new Gtk::ComboBoxText());
  apply->append("Paragraph");
  if (have_cell)
    apply->append("Cell");
  auto* apply_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  apply_row->set_margin_top(8);
  apply_row->set_margin_bottom(12);
  apply_row->set_margin_start(12);
  apply_row->set_margin_end(12);
  apply_row->pack_start(*label("_Apply to:", *apply), Gtk::PACK_SHRINK);
  apply_row->pack_start(*apply, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*book, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*apply_row, Gtk::PACK_SHRINK);

  struct Chrome {
    Borders borders;
    Shading shading;
  };
  Chrome working;
  bool filling = false;

  auto select_width = [&](int twips) {
    const int want = twips > 0 ? twips : kDefaultBorderWidth;
    for (size_t i = 0; i < width_values.size(); ++i) {
      if (width_values[i] == want) {
        width_combo->set_active(static_cast<int>(i));
        return;
      }
    }
    width_values.push_back(want);
    width_combo->append(width_label(want));
    width_combo->set_active(static_cast<int>(width_values.size()) - 1);
  };
  auto select_pattern = [&](int pattern) {
    for (size_t i = 0; i < pattern_values.size(); ++i) {
      if (pattern_values[i] == pattern) {
        pattern_combo->set_active(static_cast<int>(i));
        return;
      }
    }
    pattern_values.push_back(pattern);
    pattern_combo->append(pattern_label(pattern));
    pattern_combo->set_active(static_cast<int>(pattern_values.size()) - 1);
  };
  auto chosen_style = [&]() {
    const int row = style_combo->get_active_row_number();
    if (row < 0)
      return BorderStyle::Single;
    return clamp_border_style(row + 1);
  };
  auto chosen_width = [&]() {
    const int row = width_combo->get_active_row_number();
    if (row < 0 || row >= static_cast<int>(width_values.size()))
      return kDefaultBorderWidth;
    return width_values[static_cast<size_t>(row)];
  };
  auto chosen_ink = [](Gtk::ComboBoxText& combo) {
    const int row = combo.get_active_row_number();
    return ink_clamp(row < 0 ? 0 : row);
  };
  auto sensitize = [&]() {
    const Setting setting =
        static_cast<Setting>(std::max(0, setting_combo->get_active_row_number()));
    const bool none = setting == Setting::None;
    const bool custom = setting == Setting::Custom;
    style_combo->set_sensitive(!none && setting != Setting::ThreeD);
    color_combo->set_sensitive(!none);
    width_combo->set_sensitive(!none);
    top_check->set_sensitive(custom);
    left_check->set_sensitive(custom);
    bottom_check->set_sensitive(custom);
    right_check->set_sensitive(custom);
  };
  auto pull_borders = [&]() {
    if (filling)
      return;
    const Setting setting =
        static_cast<Setting>(std::max(0, setting_combo->get_active_row_number()));
    if (setting == Setting::None) {
      working.borders = Borders{};
      return;
    }
    const BorderStyle style = chosen_style();
    const int width = chosen_width();
    const Ink color = chosen_ink(*color_combo);
    auto paint = [&](BorderSide& side, bool on, BorderStyle which, bool shadow) {
      const int space = clamp_side(side).space;
      if (!on) {
        side = BorderSide{};
        return;
      }
      side.style = which;
      side.width = width;
      side.color = color;
      side.space = space;
      side.shadow = shadow;
      side = clamp_side(side);
    };
    if (setting == Setting::ThreeD) {
      paint(working.borders.top, true, BorderStyle::Inset, false);
      paint(working.borders.left, true, BorderStyle::Inset, false);
      paint(working.borders.bottom, true, BorderStyle::Outset, false);
      paint(working.borders.right, true, BorderStyle::Outset, false);
      return;
    }
    if (setting == Setting::Box || setting == Setting::Shadow) {
      const bool shadow = setting == Setting::Shadow;
      paint(working.borders.top, true, style, shadow);
      paint(working.borders.left, true, style, shadow);
      paint(working.borders.bottom, true, style, shadow);
      paint(working.borders.right, true, style, shadow);
      return;
    }
    auto custom = [&](BorderSide& side, bool on) {
      const bool shadow = side.shadow;
      paint(side, on, style, shadow);
    };
    custom(working.borders.top, top_check->get_active());
    custom(working.borders.left, left_check->get_active());
    custom(working.borders.bottom, bottom_check->get_active());
    custom(working.borders.right, right_check->get_active());
  };
  auto pull_shading = [&]() {
    if (filling)
      return;
    const int row = pattern_combo->get_active_row_number();
    const int pattern = row >= 0 && row < static_cast<int>(pattern_values.size())
                            ? pattern_values[static_cast<size_t>(row)]
                            : 0;
    working.shading.pattern = pattern;
    working.shading.background = chosen_ink(*fill_combo);
    working.shading.foreground = chosen_ink(*pattern_color);
    working.shading = clamp_shading(working.shading);
  };
  auto show_checks = [&]() {
    const bool was = filling;
    filling = true;
    top_check->set_active(working.borders.top.style != BorderStyle::None);
    left_check->set_active(working.borders.left.style != BorderStyle::None);
    bottom_check->set_active(working.borders.bottom.style != BorderStyle::None);
    right_check->set_active(working.borders.right.style != BorderStyle::None);
    filling = was;
  };
  auto load = [&](bool cell) {
    filling = true;
    if (cell && !cell_set)
      working.borders = border_box(BorderStyle::Single, kDefaultBorderWidth, Ink::Auto);
    else if (cell)
      working.borders = cell_borders;
    else
      working.borders = paragraph.borders;
    working.shading = cell ? cell_shading : paragraph.shading;
    working.borders = clamp_borders(working.borders);
    working.shading = clamp_shading(working.shading);
    const Setting setting = recognize(working.borders);
    setting_combo->set_active(static_cast<int>(setting));
    BorderSide sample;
    sample.style = BorderStyle::Single;
    sample.width = kDefaultBorderWidth;
    for (int edge = 0; edge < 4; ++edge) {
      const BorderSide side = clamp_side(border_at(working.borders, edge));
      if (side.style == BorderStyle::None)
        continue;
      sample = side;
      break;
    }
    BorderStyle shown = sample.style;
    if (shown == BorderStyle::None || setting == Setting::ThreeD)
      shown = BorderStyle::Single;
    style_combo->set_active(std::max(0, static_cast<int>(shown) - 1));
    color_combo->set_active(static_cast<int>(sample.color));
    select_width(sample.width);
    show_checks();
    fill_combo->set_active(static_cast<int>(working.shading.background));
    select_pattern(working.shading.pattern);
    pattern_color->set_active(static_cast<int>(working.shading.foreground));
    sensitize();
    filling = false;
  };

  setting_combo->signal_changed().connect([&] {
    if (filling)
      return;
    pull_borders();
    show_checks();
    sensitize();
  });
  auto on_line = [&]() {
    if (filling)
      return;
    pull_borders();
  };
  style_combo->signal_changed().connect(on_line);
  color_combo->signal_changed().connect(on_line);
  width_combo->signal_changed().connect(on_line);
  top_check->signal_toggled().connect(on_line);
  left_check->signal_toggled().connect(on_line);
  bottom_check->signal_toggled().connect(on_line);
  right_check->signal_toggled().connect(on_line);
  auto on_shade = [&]() {
    if (filling)
      return;
    pull_shading();
  };
  fill_combo->signal_changed().connect(on_shade);
  pattern_combo->signal_changed().connect(on_shade);
  pattern_color->signal_changed().connect(on_shade);
  apply->signal_changed().connect([&] {
    if (filling)
      return;
    load(apply->get_active_row_number() == 1);
  });

  filling = true;
  apply->set_active(have_cell ? 1 : 0);
  filling = false;
  load(have_cell);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    restore_edit_focus();
    return;
  }
  const bool cell = have_cell && apply->get_active_row_number() == 1;
  const Borders borders = clamp_borders(working.borders);
  const Shading shading = clamp_shading(working.shading);
  if (!cell) {
    apply_para_edit([&](ParaFormat& format) {
      format.borders = borders;
      format.shading = shading;
    });
    return;
  }
  TableSheet* sheet = editing_sheet();
  if (!sheet) {
    restore_edit_focus();
    return;
  }
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(*sheet, row, column, caret, bound) || cell_table == 0) {
    restore_edit_focus();
    return;
  }
  const int id = cell_table;
  const Document before = capture();
  Document after = before;
  bool touched = false;
  for (Paragraph& item : after.paragraphs) {
    if (item.cell.table != id || item.cell.row != row || item.cell.column != column)
      continue;
    item.cell.borders_set = true;
    item.cell.borders = borders;
    item.cell.shading = shading;
    touched = true;
  }
  if (!touched || before == after) {
    restore_edit_focus();
    return;
  }
  const CellEdit saved = cell_edit_;
  hold_view();
  commit_document(before, after);
  restore_cell_edit(saved);
  release_view();
}

void MainWindow::build_find()
{
  find_dialog_ = std::make_unique<Gtk::Dialog>("Find and Replace", *this, true);
  find_dialog_->set_modal(true);
  find_dialog_->set_resizable(false);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* find_label = Gtk::manage(new Gtk::Label("Find"));
  auto* replace_label = Gtk::manage(new Gtk::Label("Replace"));
  find_label->set_halign(Gtk::ALIGN_START);
  replace_label->set_halign(Gtk::ALIGN_START);
  find_entry_ = Gtk::manage(new Gtk::Entry());
  replace_entry_ = Gtk::manage(new Gtk::Entry());
  match_case_ = Gtk::manage(new Gtk::CheckButton("Match case"));
  find_entry_->set_size_request(260, -1);
  replace_entry_->set_size_request(260, -1);
  grid->attach(*find_label, 0, 0, 1, 1);
  grid->attach(*find_entry_, 1, 0, 1, 1);
  grid->attach(*replace_label, 0, 1, 1, 1);
  grid->attach(*replace_entry_, 1, 1, 1, 1);
  grid->attach(*match_case_, 0, 2, 2, 1);
  find_dialog_->get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  find_dialog_->add_button("Next", kFindNext);
  find_dialog_->add_button("Replace", kFindReplace);
  find_dialog_->add_button("Close", Gtk::RESPONSE_CLOSE);
  find_dialog_->signal_response().connect([this](int response) {
    if (response == kFindNext)
      find_next();
    else if (response == kFindReplace)
      replace_once();
    else
      find_dialog_->hide();
  });
  find_dialog_->signal_delete_event().connect(
      [this](GdkEventAny*) {
        find_dialog_->hide();
        return true;
      },
      false);
  find_dialog_->signal_key_press_event().connect(
      [this](GdkEventKey* event) {
        if (event->keyval == GDK_KEY_Escape) {
          find_dialog_->hide();
          return true;
        }
        return false;
      },
      false);
  find_entry_->signal_activate().connect([this] { find_next(); });
  replace_entry_->signal_activate().connect([this] { replace_once(); });
  find_dialog_->show_all_children();
}

void MainWindow::present_find(bool replace)
{
  if (!find_dialog_)
    build_find();
  Gtk::TextBuffer::iterator start;
  Gtk::TextBuffer::iterator end;
  if (buffer_->get_selection_bounds(start, end)) {
    const Glib::ustring selected = buffer_->get_text(start, end);
    if (!selected.empty() && selected.find('\n') == Glib::ustring::npos)
      find_entry_->set_text(selected);
  }
  find_dialog_->present();
  if (replace)
    replace_entry_->grab_focus();
  else
    find_entry_->grab_focus();
}

namespace {

struct FoldedText {
  Glib::ustring text;
  std::vector<int> origin;
};

FoldedText fold_chars(const Glib::ustring& text)
{
  FoldedText folded;
  const int count = static_cast<int>(text.length());
  for (int i = 0; i < count; ++i) {
    const Glib::ustring part = Glib::ustring(1, text[i]).casefold();
    const int part_count = static_cast<int>(part.length());
    for (int k = 0; k < part_count; ++k)
      folded.origin.push_back(i);
    folded.text.append(part);
  }
  return folded;
}

int folded_end(const FoldedText& folded, int begin, int end)
{
  int last = folded.origin[static_cast<size_t>(begin)];
  for (int i = begin; i < end; ++i)
    last = std::max(last, folded.origin[static_cast<size_t>(i)]);
  return last + 1;
}

}  // namespace

FindHit find_in_text(const Glib::ustring& doc, const Glib::ustring& needle, int origin,
                     bool match_case, bool wrap)
{
  FindHit miss;
  if (needle.empty())
    return miss;
  const int count = static_cast<int>(doc.length());
  if (origin < 0)
    origin = 0;
  if (origin > count)
    origin = count;
  if (match_case) {
    const int width = static_cast<int>(needle.length());
    if (width <= 0 || width > count)
      return miss;
    auto matches = [&](int index) {
      return doc.substr(static_cast<Glib::ustring::size_type>(index),
                        static_cast<Glib::ustring::size_type>(width)) == needle;
    };
    for (int i = origin; i + width <= count; ++i) {
      if (matches(i))
        return FindHit{i, width};
    }
    if (wrap) {
      for (int i = 0; i < std::min(origin, count); ++i) {
        if (matches(i))
          return FindHit{i, width};
      }
    }
    return miss;
  }
  const FoldedText hay = fold_chars(doc);
  const Glib::ustring want = needle.casefold();
  const int folded_count = static_cast<int>(hay.text.length());
  const int width = static_cast<int>(want.length());
  if (width <= 0 || width > folded_count)
    return miss;
  int from = 0;
  while (from < folded_count && hay.origin[static_cast<size_t>(from)] < origin)
    ++from;
  auto hit_at = [&](int folded_index) {
    const int index = hay.origin[static_cast<size_t>(folded_index)];
    return FindHit{index, folded_end(hay, folded_index, folded_index + width) - index};
  };
  for (int i = from; i + width <= folded_count; ++i) {
    if (hay.text.substr(static_cast<Glib::ustring::size_type>(i),
                        static_cast<Glib::ustring::size_type>(width)) == want)
      return hit_at(i);
  }
  if (wrap) {
    for (int i = 0; i < std::min(from, folded_count); ++i) {
      if (hay.text.substr(static_cast<Glib::ustring::size_type>(i),
                          static_cast<Glib::ustring::size_type>(width)) == want)
        return hit_at(i);
    }
  }
  return miss;
}

PieceHit find_in_pieces(const std::vector<FindPiece>& pieces, const Glib::ustring& needle,
                        int start_id, int origin, bool match_case)
{
  PieceHit miss;
  if (needle.empty() || pieces.empty())
    return miss;
  int start = 0;
  for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
    if (pieces[static_cast<size_t>(i)].id == start_id) {
      start = i;
      break;
    }
  }
  auto take = [&](int piece, const FindHit& found) {
    return PieceHit{pieces[static_cast<size_t>(piece)].id, found.index, found.length};
  };
  const FindHit in_start =
      find_in_text(pieces[static_cast<size_t>(start)].text, needle, origin, match_case, false);
  if (in_start.index >= 0)
    return take(start, in_start);
  for (int piece = start + 1; piece < static_cast<int>(pieces.size()); ++piece) {
    const FindHit found =
        find_in_text(pieces[static_cast<size_t>(piece)].text, needle, 0, match_case, false);
    if (found.index >= 0)
      return take(piece, found);
  }
  for (int piece = 0; piece < start; ++piece) {
    const FindHit found =
        find_in_text(pieces[static_cast<size_t>(piece)].text, needle, 0, match_case, false);
    if (found.index >= 0)
      return take(piece, found);
  }
  const FindHit wrapped =
      find_in_text(pieces[static_cast<size_t>(start)].text, needle, 0, match_case, false);
  if (wrapped.index >= 0 && wrapped.index < origin)
    return take(start, wrapped);
  return miss;
}

void MainWindow::find_next()
{
  if (!find_entry_ || !buffer_)
    return;
  const Glib::ustring needle = find_entry_->get_text();
  if (needle.empty())
    return;
  const bool match_case = match_case_ && match_case_->get_active();
  const std::vector<Gtk::TextView*> views = find_targets();
  std::vector<FindPiece> pieces;
  pieces.reserve(views.size());
  int focused_view = -1;
  int focused_caret = 0;
  Gtk::Widget* const focused = get_focus();
  for (int i = 0; i < static_cast<int>(views.size()); ++i) {
    Gtk::TextView* view = views[static_cast<size_t>(i)];
    Glib::ustring text;
    if (view && view->get_buffer()) {
      auto buffer = view->get_buffer();
      // get_text() drops pictures and table anchors. The slice keeps them as
      // U+FFFC, so a match after one is at the buffer offset Find selects.
      text = buffer->get_slice(buffer->begin(), buffer->end(), true);
      if (view == focused) {
        focused_view = i;
        Gtk::TextBuffer::iterator sel_start;
        Gtk::TextBuffer::iterator sel_end;
        if (buffer->get_selection_bounds(sel_start, sel_end))
          focused_caret = sel_end.get_offset();
        else
          focused_caret = buffer->get_insert()->get_iter().get_offset();
      }
    }
    pieces.push_back(FindPiece{i, text});
  }
  const int body_caret = buffer_->get_insert()->get_iter().get_offset();
  const FindResume resume = find_resume(focused_view >= 0, focused_view, focused_caret, find_view_,
                                        find_origin_, body_caret);
  const int start = resume.view;
  const int origin = resume.origin;
  const PieceHit hit = find_in_pieces(pieces, needle, start, origin, match_case);
  if (hit.id < 0 || hit.id >= static_cast<int>(views.size())) {
    message_.set_text("No matches.");
    return;
  }
  Gtk::TextView* const target = views[static_cast<size_t>(hit.id)];
  if (!target || !target->get_buffer()) {
    message_.set_text("No matches.");
    return;
  }
  for (Gtk::TextView* other : views) {
    if (!other || other == target || !other->get_buffer())
      continue;
    other->get_buffer()->place_cursor(other->get_buffer()->get_insert()->get_iter());
  }
  auto start_iter = target->get_buffer()->get_iter_at_offset(hit.index);
  auto end_iter = target->get_buffer()->get_iter_at_offset(hit.index + hit.length);
  target->get_buffer()->select_range(start_iter, end_iter);
  find_view_ = hit.id;
  find_origin_ = hit.index + hit.length;
  follow_caret_ = true;
  target->grab_focus();
  target->scroll_to(start_iter);
  if (target == &text_)
    scroll_to_caret();
  else
    reveal_story_caret(*target);
  if (message_.get_text() == "No matches.")
    message_.set_text("");
}

bool selection_matches(const Glib::ustring& selected, const Glib::ustring& needle, bool match_case)
{
  if (needle.empty())
    return false;
  if (match_case)
    return selected == needle;
  return selected.casefold() == needle.casefold();
}

void MainWindow::replace_once()
{
  if (!find_entry_ || !replace_entry_ || !buffer_)
    return;
  const Glib::ustring needle = find_entry_->get_text();
  if (needle.empty())
    return;
  const Glib::ustring replacement = replace_entry_->get_text();
  const bool match_case = match_case_ && match_case_->get_active();
  Gtk::TextView* view = nullptr;
  Gtk::TextBuffer::iterator start;
  Gtk::TextBuffer::iterator end;
  for (Gtk::TextView* candidate : find_targets()) {
    if (!candidate || !candidate->get_buffer())
      continue;
    Gtk::TextBuffer::iterator from;
    Gtk::TextBuffer::iterator to;
    if (!candidate->get_buffer()->get_selection_bounds(from, to))
      continue;
    auto candidate_buffer = candidate->get_buffer();
    const Glib::ustring selected = candidate_buffer->get_slice(from, to, true);
    if (!selection_matches(selected, needle, match_case))
      continue;
    view = candidate;
    start = from;
    end = to;
    break;
  }
  if (view && view->get_buffer()) {
    auto buffer = view->get_buffer();
    if (view == &text_) {
      const Run format = format_of(start);
      const int heading = heading_of(start);
      const int at = start.get_offset();
      const int len = static_cast<int>(replacement.length());
      buffer_->begin_user_action();
      buffer_->erase(start, end);
      if (len > 0) {
        auto iter = buffer_->get_iter_at_offset(at);
        buffer_->insert(iter, replacement);
        // Inserting beside an existing run inherits that run's tag. Keep the
        // match's own format as the only character tag on the new text.
        auto from = buffer_->get_iter_at_offset(at);
        auto to = buffer_->get_iter_at_offset(at + len);
        set_fmt(from, to, format_tag(format));
        from = buffer_->get_iter_at_offset(at);
        to = buffer_->get_iter_at_offset(at + len);
        if (heading > 0)
          buffer_->apply_tag(heading_tag(heading), from, to);
      }
      buffer_->end_user_action();
    } else if (view == &header_view_ || view == &footer_view_ || is_note_view(view)) {
      // A header, footer, or note stays plain text until that story is typed
      // in. Replace there does not invent character tags.
      const int at = start.get_offset();
      const int len = static_cast<int>(replacement.length());
      buffer->begin_user_action();
      buffer->erase(start, end);
      if (len > 0) {
        auto iter = buffer->get_iter_at_offset(at);
        buffer->insert(iter, replacement);
      }
      buffer->end_user_action();
    } else {
      const Run format = format_of(start);
      const int at = start.get_offset();
      const int len = static_cast<int>(replacement.length());
      buffer->begin_user_action();
      buffer->erase(start, end);
      if (len > 0) {
        auto iter = buffer->get_iter_at_offset(at);
        buffer->insert(iter, replacement);
        auto from = buffer->get_iter_at_offset(at);
        auto to = buffer->get_iter_at_offset(at + len);
        set_fmt_on(buffer, from, to, format_tag_on(buffer, format));
      }
      buffer->end_user_action();
    }
  }
  find_next();
}

void MainWindow::on_options()
{
  Gtk::Dialog dialog("Options", *this, true);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* font_label = Gtk::manage(new Gtk::Label("Default font"));
  auto* size_label = Gtk::manage(new Gtk::Label("Default size"));
  auto* recent_label = Gtk::manage(new Gtk::Label("Recent files"));
  auto* units_label = Gtk::manage(new Gtk::Label("Measurement units"));
  auto* dictionary_label = Gtk::manage(new Gtk::Label("Dictionary"));
  font_label->set_halign(Gtk::ALIGN_START);
  size_label->set_halign(Gtk::ALIGN_START);
  recent_label->set_halign(Gtk::ALIGN_START);
  units_label->set_halign(Gtk::ALIGN_START);
  dictionary_label->set_halign(Gtk::ALIGN_START);
  auto* font = Gtk::manage(new Gtk::ComboBoxText());
  auto* size = Gtk::manage(new Gtk::ComboBoxText());
  auto* recent = Gtk::manage(new Gtk::ComboBoxText());
  fill_font_combo(*font, settings_.default_font);
  for (const char* item : {"8", "9", "10", "11", "12", "14", "16", "18", "24", "36"})
    size->append(item);
  size->set_active_text(std::to_string(settings_.default_size));
  recent->append("4");
  recent->append("8");
  recent->append("12");
  recent->set_active_text(std::to_string(settings_.recent_count));
  auto* units = Gtk::manage(new Gtk::ComboBoxText());
  units->append("in", "Inches");
  units->append("cm", "Centimetres");
  units->set_active_id(units_text(settings_.units));
  font->set_size_request(220, -1);
  grid->attach(*font_label, 0, 0, 1, 1);
  grid->attach(*font, 1, 0, 1, 1);
  grid->attach(*size_label, 0, 1, 1, 1);
  grid->attach(*size, 1, 1, 1, 1);
  grid->attach(*recent_label, 0, 2, 1, 1);
  grid->attach(*recent, 1, 2, 1, 1);
  auto* dictionary = Gtk::manage(new Gtk::Entry());
  dictionary->set_text(settings_.dictionary);
  grid->attach(*units_label, 0, 3, 1, 1);
  grid->attach(*units, 1, 3, 1, 1);
  grid->attach(*dictionary_label, 0, 4, 1, 1);
  grid->attach(*dictionary, 1, 4, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all_children();
  if (dialog.run() != Gtk::RESPONSE_OK)
    return;
  const std::string chosen_font = font->get_active_text();
  if (!chosen_font.empty())
    settings_.default_font = chosen_font;
  try {
    const int chosen_size = std::stoi(size->get_active_text().raw());
    if (known_size(chosen_size))
      settings_.default_size = chosen_size;
  } catch (const std::exception&) {
  }
  try {
    const int count = std::stoi(recent->get_active_text().raw());
    if (count == 4 || count == 8 || count == 12)
      settings_.recent_count = count;
  } catch (const std::exception&) {
  }
  reload_recent();
  if (static_cast<int>(settings_.recent.size()) > settings_.recent_count)
    settings_.recent.resize(static_cast<size_t>(settings_.recent_count));
  settings_.units = units_from_text(units->get_active_id().raw());
  std::string chosen_dictionary = dictionary->get_text();
  const auto trim = [](std::string value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
      value.erase(value.begin());
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
      value.pop_back();
    return value;
  };
  chosen_dictionary = trim(std::move(chosen_dictionary));
  if (chosen_dictionary.empty())
    settings_.dictionary = "en";
  else if (dictionary_name_ok(chosen_dictionary))
    settings_.dictionary = chosen_dictionary;
  settings_.save_fields(kSetFont | kSetSize | kSetRecentCount | kSetRecent | kSetUnits |
                        kSetDictionary);
  rebuild_recent();
  if (save_path_.empty() && save_point_ && !dirty()) {
    typing_.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
    typing_.size = settings_.default_size;
    typing_.bold = false;
    typing_.italic = false;
    typing_.underline = false;
    show_format(typing_);
  }
}

void MainWindow::on_spelling()
{
  if (!buffer_)
    return;
  const std::string name = dictionary_name_ok(settings_.dictionary) ? settings_.dictionary : "en";
  AspellDictionary dictionary(name);
  if (!dictionary.available()) {
    tell(dictionary.error().empty() ? "Could not find aspell." : dictionary.error());
    return;
  }

  std::unordered_set<std::string> ignored;
  std::unordered_map<std::string, bool> cache;
  auto known = [&](const std::string& word) {
    const std::string folded = fold_word(word);
    if (ignored.count(folded) != 0)
      return true;
    const auto found = cache.find(folded);
    if (found != cache.end())
      return found->second;
    const bool yes = dictionary.contains(word);
    cache[folded] = yes;
    return yes;
  };

  Document doc = capture();
  std::vector<SpellMiss> misses = spelling_misses(doc, known);
  size_t index = 0;

  constexpr int kIgnore = 10;
  constexpr int kIgnoreAll = 11;
  constexpr int kChange = 12;
  constexpr int kChangeAll = 13;
  Gtk::Dialog dialog("Spelling", *this, true);
  dialog.set_resizable(false);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* word_label = Gtk::manage(new Gtk::Label("Not in dictionary"));
  auto* change_label = Gtk::manage(new Gtk::Label("Change to"));
  auto* suggest_label = Gtk::manage(new Gtk::Label("Suggestions"));
  word_label->set_halign(Gtk::ALIGN_START);
  change_label->set_halign(Gtk::ALIGN_START);
  suggest_label->set_halign(Gtk::ALIGN_START);
  auto* word = Gtk::manage(new Gtk::Label());
  word->set_halign(Gtk::ALIGN_START);
  word->set_max_width_chars(28);
  word->set_ellipsize(Pango::ELLIPSIZE_END);
  auto* change = Gtk::manage(new Gtk::Entry());
  change->set_size_request(260, -1);
  auto* suggestions = Gtk::manage(new Gtk::ListBox());
  suggestions->set_selection_mode(Gtk::SELECTION_SINGLE);
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_size_request(260, 140);
  scroll->add(*suggestions);
  grid->attach(*word_label, 0, 0, 1, 1);
  grid->attach(*word, 1, 0, 1, 1);
  grid->attach(*change_label, 0, 1, 1, 1);
  grid->attach(*change, 1, 1, 1, 1);
  grid->attach(*suggest_label, 0, 2, 1, 1);
  grid->attach(*scroll, 1, 2, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  auto* ignore = dialog.add_button("_Ignore", kIgnore);
  auto* ignore_all = dialog.add_button("Ignore _All", kIgnoreAll);
  auto* change_button = dialog.add_button("_Change", kChange);
  auto* change_all = dialog.add_button("Change A_ll", kChangeAll);
  dialog.add_button("_Close", Gtk::RESPONSE_CLOSE);

  sigc::connection selected =
      suggestions->signal_row_selected().connect([change](Gtk::ListBoxRow* row) {
        if (!row)
          return;
        if (auto* label = dynamic_cast<Gtk::Label*>(row->get_child()))
          change->set_text(label->get_text());
      });

  auto clear_suggestions = [suggestions] {
    const std::vector<Gtk::Widget*> rows = suggestions->get_children();
    for (Gtk::Widget* row : rows)
      suggestions->remove(*row);
  };
  auto show = [&] {
    const bool done = index >= misses.size();
    ignore->set_sensitive(!done);
    ignore_all->set_sensitive(!done);
    change_button->set_sensitive(!done);
    change_all->set_sensitive(!done);
    change->set_sensitive(!done);
    // Enter applies Change while a word is showing, and closes once it is done.
    dialog.set_default_response(done ? Gtk::RESPONSE_CLOSE : kChange);
    clear_suggestions();
    if (done) {
      word->set_text("The spelling check is complete.");
      change->set_text("");
      return;
    }
    const SpellMiss& miss = misses[index];
    word->set_text(miss.word);
    const std::vector<std::string> offered = dictionary.suggestions(miss.word);
    for (const std::string& suggestion : offered) {
      auto* label = Gtk::manage(new Gtk::Label(suggestion));
      label->set_halign(Gtk::ALIGN_START);
      label->set_margin_start(6);
      label->show();
      suggestions->append(*label);
    }
    if (!offered.empty())
      change->set_text(offered.front());
    else
      change->set_text(miss.word);
    if (Gtk::ListBoxRow* row = suggestions->get_row_at_index(0))
      suggestions->select_row(*row);
  };

  auto same_word = [](const SpellMiss& miss, const std::string& folded) {
    return fold_word(miss.word) == folded;
  };
  auto shift_later = [](std::vector<SpellMiss>& items, const SpellMiss& miss, long long delta) {
    for (SpellMiss& other : items) {
      if (other.where != miss.where || other.note != miss.note ||
          other.paragraph != miss.paragraph || other.begin < miss.end)
        continue;
      other.begin = static_cast<size_t>(static_cast<long long>(other.begin) + delta);
      other.end = static_cast<size_t>(static_cast<long long>(other.end) + delta);
    }
  };
  auto commit_change = [&](const Document& before) {
    if (before == doc)
      return;
    commit_document(before, doc);
    cache.clear();
    // commit_document() focuses the page. The keyboard belongs in this dialog.
    change->grab_focus();
  };

  show();
  dialog.show_all_children();
  while (true) {
    const int response = dialog.run();
    if (response == Gtk::RESPONSE_CLOSE || response == Gtk::RESPONSE_DELETE_EVENT ||
        response == Gtk::RESPONSE_NONE)
      break;
    if (index >= misses.size())
      continue;
    if (response == kIgnore) {
      misses.erase(misses.begin() + static_cast<std::ptrdiff_t>(index));
    } else if (response == kIgnoreAll) {
      const std::string folded = fold_word(misses[index].word);
      ignored.insert(folded);
      cache[folded] = true;
      misses.erase(std::remove_if(misses.begin() + static_cast<std::ptrdiff_t>(index), misses.end(),
                                  [&](const SpellMiss& miss) { return same_word(miss, folded); }),
                   misses.end());
    } else if (response == kChange || response == kChangeAll) {
      const std::string replacement = change->get_text();
      const Document before = doc;
      if (response == kChange) {
        const SpellMiss miss = misses[index];
        Paragraph* paragraph = spell_paragraph(doc, miss);
        if (paragraph && replace_word(*paragraph, miss.begin, miss.end, replacement)) {
          const long long delta = static_cast<long long>(replacement.size()) -
                                  static_cast<long long>(miss.end - miss.begin);
          shift_later(misses, miss, delta);
          misses.erase(misses.begin() + static_cast<std::ptrdiff_t>(index));
        }
      } else {
        const std::string folded = fold_word(misses[index].word);
        std::vector<size_t> hits;
        for (size_t i = index; i < misses.size(); ++i) {
          if (same_word(misses[i], folded))
            hits.push_back(i);
        }
        for (size_t n = hits.size(); n-- > 0;) {
          const SpellMiss miss = misses[hits[n]];
          Paragraph* paragraph = spell_paragraph(doc, miss);
          if (!paragraph || !replace_word(*paragraph, miss.begin, miss.end, replacement))
            continue;
          const long long delta = static_cast<long long>(replacement.size()) -
                                  static_cast<long long>(miss.end - miss.begin);
          shift_later(misses, miss, delta);
        }
        misses.erase(
            std::remove_if(misses.begin() + static_cast<std::ptrdiff_t>(index), misses.end(),
                           [&](const SpellMiss& miss) { return same_word(miss, folded); }),
            misses.end());
      }
      commit_change(before);
    }
    show();
  }
  selected.disconnect();
}

}  // namespace writeit
