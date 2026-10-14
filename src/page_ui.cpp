/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "para_check.hpp"

#include <giomm/memoryinputstream.h>

#include <gtkmm/gesturedrag.h>

extern "C" {
typedef struct _GtkTextLayout GtkTextLayout;
void gtk_text_child_anchor_queue_resize(GtkTextChildAnchor* anchor, GtkTextLayout* layout);
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace writeit {

int chars_of(const Run& run)
{
  if (run.image)
    return 1;
  return static_cast<int>(Glib::ustring(run.text).length());
}

int chars_of(const Paragraph& paragraph)
{
  int count = 0;
  for (const Run& run : paragraph.runs)
    count += chars_of(run);
  return count;
}

// Buffer offset where paragraph `index` starts. A table is one anchor
// character. Its cells share that offset: the caret is the anchor, not a
// character past it. A newline after the anchor, when another paragraph
// follows, is only a separator.
int chars_before(const std::vector<Paragraph>& paragraphs, size_t index)
{
  int offset = 0;
  size_t i = 0;
  const size_t end = std::min(index, paragraphs.size());
  while (i < end) {
    if (paragraphs[i].cell.table != 0) {
      const int id = paragraphs[i].cell.table;
      size_t j = i + 1;
      while (j < paragraphs.size() && paragraphs[j].cell.table == id)
        ++j;
      if (end < j)
        return offset;
      offset += 1;
      if (j < paragraphs.size())
        ++offset;
      i = j;
      continue;
    }
    offset += chars_of(paragraphs[i]) + 1;
    ++i;
  }
  return offset;
}

namespace {

constexpr int kPicturePx = 480;

struct PaperPreset {
  const char* name;
  int width;
  int height;
};

constexpr PaperPreset kPapers[] = {
    {"A4", 11906, 16838},
    {"Letter", 12240, 15840},
    {"Legal", 12240, 20160},
    {"Custom", 0, 0},
};

TableSheet* sheet_from(const Glib::RefPtr<Gtk::TextChildAnchor>& anchor)
{
  if (!anchor)
    return nullptr;
  return static_cast<TableSheet*>(g_object_get_data(G_OBJECT(anchor->gobj()), "writeit-table"));
}

bool blank_text(const std::string& text)
{
  for (unsigned char c : text) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v')
      return false;
  }
  return true;
}

bool story_present(const std::vector<Paragraph>& story)
{
  for (const Paragraph& paragraph : story) {
    for (const Run& run : paragraph.runs) {
      if (run.image || run.note != 0 || !blank_text(run.text))
        return true;
    }
  }
  return false;
}

std::string story_text(const std::vector<Paragraph>& story)
{
  std::string out;
  for (size_t i = 0; i < story.size(); ++i) {
    if (i)
      out.push_back('\n');
    for (const Run& run : story[i].runs) {
      if (!run.image)
        out += run.text;
    }
  }
  return out;
}

std::string notes_text(const std::vector<std::vector<Paragraph>>& notes)
{
  std::string out;
  for (size_t i = 0; i < notes.size(); ++i) {
    if (i)
      out += "\n\n";
    out += story_text(notes[i]);
  }
  return out;
}

Run plain_run(const Run& typing, const std::string& text)
{
  Run run = typing;
  run.text = text;
  run.note = 0;
  run.image.reset();
  run.direct = 0;
  return run;
}

std::vector<Paragraph> paragraphs_from_lines(const std::string& text, const Run& typing)
{
  std::vector<Paragraph> out;
  std::string line;
  auto flush = [&] {
    Paragraph paragraph;
    if (!line.empty())
      paragraph.runs.push_back(plain_run(typing, line));
    out.push_back(std::move(paragraph));
    line.clear();
  };
  for (char c : text) {
    if (c == '\n')
      flush();
    else
      line.push_back(c);
  }
  flush();
  return out;
}

std::vector<std::vector<Paragraph>> notes_from_text(const std::string& text, const Run& typing)
{
  std::vector<std::vector<Paragraph>> notes;
  if (blank_text(text))
    return notes;
  size_t start = 0;
  while (notes.size() < static_cast<size_t>(kMaxNotes)) {
    const size_t found = text.find("\n\n", start);
    const size_t end = found == std::string::npos ? text.size() : found;
    notes.push_back(paragraphs_from_lines(text.substr(start, end - start), typing));
    if (found == std::string::npos)
      break;
    start = found + 2;
  }
  return notes;
}

int goal_twips(int pixels)
{
  if (pixels <= 0)
    return 0;
  if (pixels > kMaxIndent / 15)
    return kMaxIndent;
  return pixels * 15;
}

Glib::RefPtr<Gdk::Pixbuf> placeholder_pixbuf()
{
  try {
    auto pix = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 48, 24);
    if (pix)
      pix->fill(0xccccccff);
    return pix;
  } catch (const Glib::Error&) {
    return {};
  }
}

// The string form of add_data stops at the first zero byte. A PNG has zero
// bytes in its header, so that call yields the gray stand-in above.
Glib::RefPtr<Gdk::Pixbuf> pixbuf_from_bytes(const std::string& bytes)
{
  if (bytes.empty())
    return {};
  try {
    auto* copy = static_cast<guint8*>(g_malloc(bytes.size()));
    if (copy == nullptr)
      return {};
    std::memcpy(copy, bytes.data(), bytes.size());
    auto stream = Gio::MemoryInputStream::create();
    stream->add_data(copy, static_cast<gssize>(bytes.size()), &g_free);
    return Gdk::Pixbuf::create_from_stream(stream);
  } catch (const Glib::Error&) {
    return {};
  }
}

// Inserts `incoming` at character `at` without merging it into a neighbour.
void insert_run(Paragraph& paragraph, int at, const Run& incoming)
{
  if (at < 0)
    at = 0;
  std::vector<Run> runs;
  bool placed = false;
  int seen = 0;
  for (const Run& run : paragraph.runs) {
    const int n = chars_of(run);
    if (placed || at >= seen + n) {
      if (!run.text.empty() || run.image || run.note != 0)
        runs.push_back(run);
      seen += n;
      continue;
    }
    if (at == seen || run.image) {
      runs.push_back(incoming);
      if (!run.text.empty() || run.image || run.note != 0)
        runs.push_back(run);
      placed = true;
      seen += n;
      continue;
    }
    const Glib::ustring text(run.text);
    const int into = at - seen;
    if (into > 0) {
      Run left = run;
      left.text = text.substr(0, into).raw();
      runs.push_back(left);
    }
    runs.push_back(incoming);
    if (into < static_cast<int>(text.length())) {
      Run right = run;
      right.text = text.substr(into).raw();
      if (into > 0)
        right.note = 0;
      right.image.reset();
      runs.push_back(right);
    }
    placed = true;
    seen += n;
  }
  if (!placed)
    runs.push_back(incoming);
  paragraph.runs = std::move(runs);
}

// The characters from `at` move to `right`, which starts on a new page.
void split_at(Paragraph& left, int at, Paragraph& right)
{
  right = left;
  right.runs.clear();
  right.page_break = true;
  right.mark.reset();
  std::vector<Run> kept;
  bool splitting = true;
  int seen = 0;
  for (const Run& run : left.runs) {
    const int n = chars_of(run);
    if (!splitting) {
      right.runs.push_back(run);
      continue;
    }
    if (seen + n <= at) {
      kept.push_back(run);
      seen += n;
      continue;
    }
    const int into = at - seen;
    if (run.image) {
      if (into <= 0)
        right.runs.push_back(run);
      else
        kept.push_back(run);
    } else {
      const Glib::ustring text(run.text);
      if (into > 0) {
        Run part = run;
        part.text = text.substr(0, into).raw();
        kept.push_back(part);
      }
      if (into < static_cast<int>(text.length())) {
        Run part = run;
        part.text = text.substr(into).raw();
        if (into > 0)
          part.note = 0;
        right.runs.push_back(part);
      }
    }
    splitting = false;
  }
  left.runs = std::move(kept);
}

Gtk::SpinButton* measure_spin(Units units, int twips, double upper)
{
  const double step = units_step(units);
  auto* button =
      Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(0, 0, upper, step, step * 5), step,
                                      static_cast<guint>(units_digits(units))));
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
  button->set_activates_default(true);
  return button;
}

Gtk::Label* field_label(const char* text, Gtk::Widget& target)
{
  auto* item = Gtk::manage(new Gtk::Label(text, true));
  item->set_halign(Gtk::ALIGN_START);
  item->set_mnemonic_widget(target);
  return item;
}

void warn(Gtk::Dialog& dialog, const std::string& sentence, Gtk::Widget& field)
{
  Gtk::MessageDialog message(dialog, sentence, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
  message.set_title(dialog.get_title());
  message.run();
  message.hide();
  field.grab_focus();
  if (auto* spin = dynamic_cast<Gtk::SpinButton*>(&field))
    spin->select_region(0, -1);
}

std::string paper_range(Units units)
{
  return "The measurement must be between " +
         format_measure(twips_to_units(kMinPaperTwips, units), units) + " and " +
         format_measure(twips_to_units(kMaxPaperTwips, units), units) + ".";
}

std::string text_room(Units units)
{
  return "The margins must leave at least " +
         format_measure(twips_to_units(kMinTextTwips, units), units) + " for the text.";
}

bool read_measure(Gtk::SpinButton& button, Units units, int original, double shown, int& twips,
                  std::string& message)
{
  double parsed = 0;
  const MeasureCheck check = check_measure(button.get_text().raw(), units, parsed);
  if (check != MeasureCheck::Ok) {
    message = measure_message(check, units);
    return false;
  }
  twips = keep_twips(original, shown, parsed, units);
  return true;
}

}  // namespace

namespace {

// One cell's frame reports the column width as both its minimum and its
// natural width, so the text view wraps inside the column instead of growing
// to the unwrapped line.
class CellFrame : public Gtk::Frame {
 public:
  CellFrame()
  {
    set_shadow_type(Gtk::SHADOW_IN);
    get_style_context()->add_class("table-cell");
    set_hexpand(false);
    set_vexpand(false);
  }

  void set_column_px(int px)
  {
    width_ = std::max(36, px);
    queue_resize();
  }

 protected:
  Gtk::SizeRequestMode get_request_mode_vfunc() const override
  {
    return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
  }

  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override
  {
    minimum_width = natural_width = width_;
  }

 private:
  int width_ = 36;
};

int px_to_twips(int px, double zoom)
{
  const double z = std::max(0.01, zoom);
  return static_cast<int>(std::lround(static_cast<double>(px) * kScreenPageTwips /
                                      (static_cast<double>(kScreenPageWidth) * z)));
}

int event_width(GdkEvent* event)
{
  if (!event || !event->any.window)
    return 0;
  return gdk_window_get_width(event->any.window);
}

// The right 3 px of a column is that column's edge. The left 3 px of any
// later column is the previous edge. Four px in from the text still places
// the caret, which is where a click in the cell is tested.
int column_edge(int column, int columns, double x, int width)
{
  if (column < 0 || column >= columns || width <= 0)
    return -1;
  if (x >= static_cast<double>(width - 3))
    return column;
  if (x < 3.0 && column > 0)
    return column - 1;
  return -1;
}

int buffer_paragraphs(const Glib::RefPtr<Gtk::TextBuffer>& buffer)
{
  if (!buffer || buffer->get_char_count() == 0)
    return 1;
  int newlines = 0;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    if (iter.get_char() == '\n')
      ++newlines;
  }
  return newlines + 1;
}

}  // namespace

// The cells of one table, side by side. The body buffer holds a single
// anchor character; this grid is the child widget there.
class TableSheet : public Gtk::Grid {
 public:
  TableSheet(MainWindow& window, const std::vector<Paragraph>& cells)
      : window_(window)
  {
    set_row_spacing(0);
    set_column_spacing(0);
    set_hexpand(false);
    set_vexpand(false);
    set_halign(Gtk::ALIGN_START);
    set_valign(Gtk::ALIGN_START);
    int id = 0;
    for (const Paragraph& paragraph : cells) {
      rows_ = std::max(rows_, std::max(paragraph.cell.rows, paragraph.cell.row + 1));
      cols_ = std::max(cols_, std::max(paragraph.cell.columns, paragraph.cell.column + 1));
      if (id == 0)
        id = paragraph.cell.table;
      if (widths_.empty() && !paragraph.cell.widths.empty())
        widths_ = paragraph.cell.widths;
    }
    if (rows_ < 1)
      rows_ = 1;
    if (cols_ < 1)
      cols_ = 1;
    views_.assign(static_cast<size_t>(rows_),
                  std::vector<Gtk::TextView*>(static_cast<size_t>(cols_)));
    frames_.assign(static_cast<size_t>(rows_), std::vector<CellFrame*>(static_cast<size_t>(cols_)));
    forms_.assign(static_cast<size_t>(rows_),
                  std::vector<std::vector<Paragraph>>(static_cast<size_t>(cols_)));
    for (const Paragraph& paragraph : cells) {
      const int row = std::max(0, std::min(paragraph.cell.row, rows_ - 1));
      const int column = std::max(0, std::min(paragraph.cell.column, cols_ - 1));
      forms_[static_cast<size_t>(row)][static_cast<size_t>(column)].push_back(paragraph);
    }
    for (int row = 0; row < rows_; ++row) {
      for (int column = 0; column < cols_; ++column) {
        auto& slot = forms_[static_cast<size_t>(row)][static_cast<size_t>(column)];
        if (slot.empty()) {
          Paragraph blank;
          blank.cell.table = id;
          blank.cell.row = row;
          blank.cell.column = column;
          blank.cell.rows = rows_;
          blank.cell.columns = cols_;
          blank.cell.widths = widths_;
          slot.push_back(blank);
        }
        auto* frame = Gtk::manage(new CellFrame());
        auto* view = Gtk::manage(new Gtk::TextView());
        view->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
        view->set_accepts_tab(false);
        view->set_left_margin(4);
        view->set_right_margin(4);
        view->set_top_margin(2);
        view->set_bottom_margin(2);
        view->set_hexpand(true);
        view->set_vexpand(true);
        view->get_style_context()->add_class("table-cell-text");
        frame->add(*view);
        attach(*frame, column, row, 1, 1);
        frames_[static_cast<size_t>(row)][static_cast<size_t>(column)] = frame;
        views_[static_cast<size_t>(row)][static_cast<size_t>(column)] = view;
        window_.fill_cell(*view, slot);
        const int at_row = row;
        const int at_column = column;
        view->signal_key_press_event().connect([this, at_row, at_column](GdkEventKey* event) {
          return on_key(event, at_row, at_column);
        });
        auto buffer = view->get_buffer();
        buffer->signal_begin_user_action().connect([this] { window_.on_cell_begin(); });
        buffer->signal_end_user_action().connect([this] { window_.on_cell_end(); });
        buffer->signal_insert().connect(
            [this, at_row, at_column](const Gtk::TextIter& end, const Glib::ustring& text, int) {
              window_.tag_cell_insert(*this, at_row, at_column, end, text);
            },
            true);
        buffer->signal_mark_set().connect([this, at_row, at_column](
                                              const Gtk::TextBuffer::iterator&,
                                              const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark) {
          Gtk::TextView* cell = views_[static_cast<size_t>(at_row)][static_cast<size_t>(at_column)];
          if (!cell)
            return;
          auto live = cell->get_buffer();
          if (live && (mark == live->get_insert() || mark == live->get_selection_bound()))
            window_.note_cell_marks(*this, at_row, at_column);
        });
        view->signal_focus_in_event().connect([this, at_row, at_column](GdkEventFocus*) {
          window_.note_cell_focus(*this, at_row, at_column);
          return false;
        });
        view->signal_focus_out_event().connect([this, at_row, at_column](GdkEventFocus*) {
          window_.note_cell_blur(*this, at_row, at_column);
          return false;
        });
        // The cell places the caret, then this click bubbles to the body
        // text view, whose gesture takes the focus back. Keep the click.
        const Gdk::EventMask watch = Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
                                     Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_MOTION_MASK;
        view->add_events(watch);
        frame->add_events(watch);
        // A press on the edge only arms a drag. The caret is placed as before,
        // and a release without movement does not resize.
        view->signal_button_press_event().connect([this, at_column, frame](GdkEventButton* event) {
          return on_cell_press(event, at_column, frame);
        });
        view->signal_button_press_event().connect([](GdkEventButton*) { return true; }, true);
        view->signal_button_release_event().connect(
            [this](GdkEventButton* event) { return on_cell_release(event); });
        view->signal_button_release_event().connect([](GdkEventButton*) { return true; }, true);
        view->signal_motion_notify_event().connect(
            [this, at_column](GdkEventMotion* event) { return on_cell_motion(event, at_column); });
        view->signal_motion_notify_event().connect([](GdkEventMotion*) { return true; }, true);
        frame->signal_button_press_event().connect([this, at_column, frame](GdkEventButton* event) {
          return on_cell_press(event, at_column, frame);
        });
        frame->signal_button_press_event().connect([view](GdkEventButton* event) {
          if (!event || event->type != GDK_BUTTON_PRESS)
            return false;
          if (event->button == 1) {
            view->grab_focus();
            view->get_buffer()->place_cursor(view->get_buffer()->begin());
          }
          return true;
        });
        frame->signal_motion_notify_event().connect(
            [this, at_column](GdkEventMotion* event) { return on_cell_motion(event, at_column); });
        frame->signal_button_release_event().connect(
            [this](GdkEventButton* event) { return on_cell_release(event); });
        // The text view's own drag claims the press and then swallows motion,
        // so a border drag has to claim the press first, and only on the edge.
        watch_edge(*view, at_column, frame);
        watch_edge(*frame, at_column, frame);
      }
    }
    relayout();
    // The body's line for this sheet does not follow a later size. Measure
    // it again once the sheet has been given its height.
    signal_size_allocate().connect([this](Gtk::Allocation&) { window_.queue_table_sync(); });
  }

  bool contains_focus() const
  {
    for (const auto& row : views_) {
      for (Gtk::TextView* view : row) {
        if (view && view->has_focus())
          return true;
      }
    }
    return false;
  }

  int paragraph_count() const
  {
    int count = 0;
    for (const auto& row : views_) {
      for (Gtk::TextView* view : row)
        count += buffer_paragraphs(view->get_buffer());
    }
    return count;
  }

  // Paragraphs of this table that sit before the caret in the focused cell.
  int index_in_table() const
  {
    int row = 0;
    int column = 0;
    if (!focused_at(row, column))
      return 0;
    int index = 0;
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < cols_; ++c) {
        Gtk::TextView* view = views_[static_cast<size_t>(r)][static_cast<size_t>(c)];
        if (r == row && c == column) {
          auto buffer = view->get_buffer();
          const auto caret = buffer->get_insert()->get_iter();
          int lines = 0;
          for (auto iter = buffer->begin(); iter.compare(caret) < 0; ++iter) {
            if (iter.get_char() == '\n')
              ++lines;
          }
          return index + lines;
        }
        index += buffer_paragraphs(view->get_buffer());
      }
    }
    return index;
  }

  void append(std::vector<Paragraph>& out) const
  {
    for (int row = 0; row < rows_; ++row) {
      for (int column = 0; column < cols_; ++column) {
        Gtk::TextView* view = views_[static_cast<size_t>(row)][static_cast<size_t>(column)];
        const auto& forms = forms_[static_cast<size_t>(row)][static_cast<size_t>(column)];
        const std::vector<Paragraph> paragraphs = window_.read_cell(*view, forms);
        out.insert(out.end(), paragraphs.begin(), paragraphs.end());
      }
    }
  }

  void focus_slot(int row, int column)
  {
    if (row < 0 || column < 0 || row >= rows_ || column >= cols_)
      return;
    if (Gtk::TextView* view = views_[static_cast<size_t>(row)][static_cast<size_t>(column)])
      view->grab_focus();
  }

  void relayout()
  {
    const double zoom = window_.zoom_factor();
    const std::string font =
        window_.settings_.default_font.empty() ? "Sans" : window_.settings_.default_font;
    const int size = window_.settings_.default_size > 0 ? window_.settings_.default_size : 11;
    Pango::FontDescription desc;
    desc.set_family(font);
    desc.set_size(static_cast<int>(static_cast<double>(size) * zoom * PANGO_SCALE));
    for (int column = 0; column < cols_; ++column) {
      int twips = kMinCellTwips;
      if (column < static_cast<int>(widths_.size()) && widths_[static_cast<size_t>(column)] > 0)
        twips = widths_[static_cast<size_t>(column)];
      const int px = std::max(36, twips_to_px(twips, zoom));
      for (int row = 0; row < rows_; ++row) {
        frames_[static_cast<size_t>(row)][static_cast<size_t>(column)]->set_column_px(px);
        Gtk::TextView* view = views_[static_cast<size_t>(row)][static_cast<size_t>(column)];
        view->override_font(desc);
        window_.restyle_buffer(view->get_buffer());
        window_.style_cell_face(*view,
                                forms_[static_cast<size_t>(row)][static_cast<size_t>(column)]);
      }
    }
    queue_resize();
  }

  int table_id() const;
  Gtk::TextView* view_at(int row, int column) const;
  const Paragraph* form_at(int row, int column, int line) const;
  int paragraphs_before(int row, int column) const;
  int line_at(int row, int column, int offset) const;
  void caret_of(int row, int column, int& caret, int& bound) const;
  bool focused_place(int& row, int& column, int& caret, int& bound) const;
  void collapse_selections(const Gtk::TextView* keep) const;

 private:
  bool focused_at(int& row, int& column) const
  {
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < cols_; ++c) {
        Gtk::TextView* view = views_[static_cast<size_t>(r)][static_cast<size_t>(c)];
        if (view && view->has_focus()) {
          row = r;
          column = c;
          return true;
        }
      }
    }
    return false;
  }

  void focus_next(int row, int column)
  {
    ++column;
    if (column >= cols_) {
      column = 0;
      ++row;
    }
    if (row < rows_)
      focus_slot(row, column);
  }

  void focus_previous(int row, int column)
  {
    --column;
    if (column < 0) {
      column = cols_ - 1;
      --row;
    }
    if (row >= 0)
      focus_slot(row, column);
  }

  bool on_key(GdkEventKey* event, int row, int column)
  {
    if (!event)
      return false;
    const guint mods = event->state & gtk_accelerator_get_default_mod_mask();
    const bool shift = (mods & GDK_SHIFT_MASK) != 0;
    const bool tab = event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_KP_Tab;
    const bool back = event->keyval == GDK_KEY_ISO_Left_Tab || (tab && shift);
    if ((mods & GDK_CONTROL_MASK) == 0 && back) {
      focus_previous(row, column);
      return true;
    }
    if ((mods & GDK_CONTROL_MASK) == 0 && tab) {
      if (row + 1 == rows_ && column + 1 == cols_) {
        // extend_table rebuilds the buffer and destroys this sheet. Leave
        // the key handler before that happens.
        TableSheet* self = this;
        Glib::signal_idle().connect_once([self] { self->window_.extend_table(*self); });
        return true;
      }
      focus_next(row, column);
      return true;
    }
    switch (story_key(event->keyval, event->state)) {
      case StoryKey::Undo: {
        MainWindow& window = window_;
        Glib::signal_idle().connect_once([&window] { window.undo(); });
        return true;
      }
      case StoryKey::Redo: {
        MainWindow& window = window_;
        Glib::signal_idle().connect_once([&window] { window.redo(); });
        return true;
      }
      case StoryKey::Pass:
        return false;
    }
    return false;
  }

  bool on_cell_press(GdkEventButton* event, int column, CellFrame* frame);
  bool on_cell_motion(GdkEventMotion* event, int column);
  bool on_cell_release(GdkEventButton* event);
  void watch_edge(Gtk::Widget& widget, int column, CellFrame* frame);
  void arm_edge(int edge, int root_x, CellFrame* frame);
  void begin_drag(GdkEvent* event);
  void apply_drag(int root_x);
  void finish_drag();

  MainWindow& window_;
  int rows_ = 0;
  int cols_ = 0;
  std::vector<int> widths_;
  bool drag_armed_ = false;
  bool drag_on_ = false;
  int drag_column_ = -1;
  int drag_root_x_ = 0;
  std::vector<int> drag_start_;
  CellFrame* drag_frame_ = nullptr;
  Gtk::GestureDrag* drag_gesture_ = nullptr;
  std::vector<Glib::RefPtr<Gtk::GestureDrag>> edge_gestures_;
  std::vector<std::vector<Gtk::TextView*>> views_;
  std::vector<std::vector<CellFrame*>> frames_;
  std::vector<std::vector<std::vector<Paragraph>>> forms_;
};

int TableSheet::table_id() const
{
  if (forms_.empty() || forms_[0].empty() || forms_[0][0].empty())
    return 0;
  return forms_[0][0][0].cell.table;
}

Gtk::TextView* TableSheet::view_at(int row, int column) const
{
  if (row < 0 || column < 0 || row >= rows_ || column >= cols_)
    return nullptr;
  return views_[static_cast<size_t>(row)][static_cast<size_t>(column)];
}

const Paragraph* TableSheet::form_at(int row, int column, int line) const
{
  if (row < 0 || column < 0 || row >= rows_ || column >= cols_)
    return nullptr;
  const auto& forms = forms_[static_cast<size_t>(row)][static_cast<size_t>(column)];
  if (forms.empty())
    return nullptr;
  if (line < 0)
    line = 0;
  if (line >= static_cast<int>(forms.size()))
    return &forms.back();
  return &forms[static_cast<size_t>(line)];
}

int TableSheet::paragraphs_before(int row, int column) const
{
  int index = 0;
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      if (r == row && c == column)
        return index;
      index +=
          buffer_paragraphs(views_[static_cast<size_t>(r)][static_cast<size_t>(c)]->get_buffer());
    }
  }
  return index;
}

int TableSheet::line_at(int row, int column, int offset) const
{
  Gtk::TextView* view = view_at(row, column);
  if (!view || !view->get_buffer())
    return 0;
  auto buffer = view->get_buffer();
  int line = 0;
  const int at = std::max(0, offset);
  for (auto iter = buffer->begin(); !iter.is_end() && iter.get_offset() < at; ++iter) {
    if (iter.get_char() == '\n')
      ++line;
  }
  const int lines = buffer_paragraphs(buffer);
  if (line >= lines)
    line = std::max(0, lines - 1);
  return line;
}

void TableSheet::caret_of(int row, int column, int& caret, int& bound) const
{
  caret = 0;
  bound = 0;
  Gtk::TextView* view = view_at(row, column);
  if (!view || !view->get_buffer())
    return;
  auto buffer = view->get_buffer();
  caret = buffer->get_insert()->get_iter().get_offset();
  bound = buffer->get_selection_bound()->get_iter().get_offset();
}

bool TableSheet::focused_place(int& row, int& column, int& caret, int& bound) const
{
  if (!focused_at(row, column))
    return false;
  caret_of(row, column, caret, bound);
  return true;
}

void TableSheet::collapse_selections(const Gtk::TextView* keep) const
{
  for (const auto& row : views_) {
    for (Gtk::TextView* view : row) {
      if (!view || view == keep)
        continue;
      auto buffer = view->get_buffer();
      if (!buffer)
        continue;
      Gtk::TextIter start;
      Gtk::TextIter end;
      if (!buffer->get_selection_bounds(start, end))
        continue;
      buffer->place_cursor(buffer->get_insert()->get_iter());
    }
  }
}

bool TableSheet::on_cell_press(GdkEventButton* event, int column, CellFrame* frame)
{
  if (!event || event->type != GDK_BUTTON_PRESS || event->button != 1)
    return false;
  const int edge =
      column_edge(column, cols_, event->x, event_width(reinterpret_cast<GdkEvent*>(event)));
  if (edge < 0)
    return false;
  arm_edge(edge, static_cast<int>(std::lround(event->x_root)), frame);
  return false;
}

bool TableSheet::on_cell_motion(GdkEventMotion* event, int column)
{
  if (!event)
    return false;
  if (drag_on_) {
    apply_drag(static_cast<int>(std::lround(event->x_root)));
    return true;
  }
  const int moved = static_cast<int>(std::lround(event->x_root)) - drag_root_x_;
  const int distance = moved < 0 ? -moved : moved;
  if (drag_armed_ && (event->state & GDK_BUTTON1_MASK) != 0 && distance >= 4) {
    begin_drag(reinterpret_cast<GdkEvent*>(event));
    apply_drag(static_cast<int>(std::lround(event->x_root)));
    return true;
  }
  const int edge =
      column_edge(column, cols_, event->x, event_width(reinterpret_cast<GdkEvent*>(event)));
  auto window = event->window ? Glib::wrap(event->window, true) : Glib::RefPtr<Gdk::Window>();
  if (edge >= 0 && window) {
    window->set_cursor(Gdk::Cursor::create(window->get_display(), Gdk::SB_H_DOUBLE_ARROW));
    return true;
  }
  if (window && !drag_armed_)
    window->set_cursor({});
  return false;
}

bool TableSheet::on_cell_release(GdkEventButton* event)
{
  if (!event || event->button != 1)
    return false;
  if (!drag_armed_ && !drag_on_)
    return false;
  const bool was_drag = drag_on_;
  finish_drag();
  return was_drag;
}

void TableSheet::arm_edge(int edge, int root_x, CellFrame* frame)
{
  drag_armed_ = true;
  drag_on_ = false;
  drag_column_ = edge;
  drag_root_x_ = root_x;
  drag_start_ = widths_;
  drag_frame_ = frame;
}

void TableSheet::watch_edge(Gtk::Widget& widget, int column, CellFrame* frame)
{
  auto gesture = Gtk::GestureDrag::create(widget);
  gesture->set_propagation_phase(Gtk::PHASE_CAPTURE);
  gesture->set_button(1);
  Gtk::GestureDrag* raw = gesture.get();
  Gtk::Widget* widget_ptr = &widget;
  edge_gestures_.push_back(gesture);
  raw->signal_begin().connect([this, raw, column, frame, widget_ptr](GdkEventSequence*) {
    GdkEvent* event = gtk_get_current_event();
    const bool ours = event && widget_ptr && gtk_get_event_widget(event) == widget_ptr->gobj();
    double x = 0;
    int width = 0;
    int root_x = 0;
    GdkWindow* hit = nullptr;
    if (ours && event->type == GDK_BUTTON_PRESS) {
      x = event->button.x;
      width = event_width(event);
      root_x = static_cast<int>(std::lround(event->button.x_root));
      hit = event->button.window;
    }
    const int edge = ours ? column_edge(column, cols_, x, width) : -1;
    if (event)
      gdk_event_free(event);
    if (edge < 0) {
      raw->set_state(Gtk::EVENT_SEQUENCE_DENIED);
      return;
    }
    if (!raw->set_state(Gtk::EVENT_SEQUENCE_CLAIMED))
      return;
    arm_edge(edge, root_x, frame);
    drag_gesture_ = raw;
    if (hit) {
      auto window = Glib::wrap(hit, true);
      if (window)
        window->set_cursor(Gdk::Cursor::create(window->get_display(), Gdk::SB_H_DOUBLE_ARROW));
    }
  });
  raw->signal_update().connect([this, raw](GdkEventSequence*) {
    if (raw != drag_gesture_ || !drag_armed_)
      return;
    double ox = 0;
    double oy = 0;
    if (!raw->get_offset(ox, oy))
      return;
    const int moved = static_cast<int>(std::lround(ox));
    const int distance = moved < 0 ? -moved : moved;
    if (!drag_on_ && distance < 4)
      return;
    drag_on_ = true;
    apply_drag(drag_root_x_ + moved);
  });
  auto finish = [this, raw](GdkEventSequence*) {
    if (raw != drag_gesture_)
      return;
    finish_drag();
  };
  raw->signal_end().connect(finish);
  raw->signal_cancel().connect(finish);
}

void TableSheet::begin_drag(GdkEvent* event)
{
  if (drag_on_)
    return;
  drag_on_ = true;
  if (!event || !event->any.window)
    return;
  auto window = Glib::wrap(event->any.window, true);
  if (!window)
    return;
  window->set_cursor(Gdk::Cursor::create(window->get_display(), Gdk::SB_H_DOUBLE_ARROW));
}

void TableSheet::apply_drag(int root_x)
{
  if (drag_column_ < 0)
    return;
  const int delta = px_to_twips(root_x - drag_root_x_, window_.zoom_factor());
  std::vector<int> next = drag_start_;
  long long sum = 0;
  for (int width : drag_start_) {
    sum += std::max(0, width);
    if (sum > 2000000000LL)
      sum = 2000000000LL;
  }
  const int room = std::max(table_width_limit(window_.page_setup_), static_cast<int>(sum));
  const int limit = std::max(kMinCellTwips * std::max(cols_, 1), room);
  if (!move_table_edge(next, drag_column_, delta, limit))
    return;
  if (next == widths_)
    return;
  widths_ = std::move(next);
  relayout();
}

void TableSheet::finish_drag()
{
  const bool changed = drag_on_ && widths_ != drag_start_;
  const int id = table_id();
  const std::vector<int> now = widths_;
  drag_armed_ = false;
  drag_on_ = false;
  drag_column_ = -1;
  drag_frame_ = nullptr;
  drag_gesture_ = nullptr;
  if (!changed)
    return;
  MainWindow& window = window_;
  // commit rebuilds the body and destroys this sheet, so it waits until the
  // event handler has returned.
  Glib::signal_idle().connect_once([&window, id, now] { window.commit_table_widths(id, now); });
}

void MainWindow::mount_table(const std::vector<Paragraph>& cells, bool separator)
{
  auto anchor = buffer_->create_child_anchor(buffer_->end());
  if (separator)
    buffer_->insert(buffer_->end(), "\n");
  auto* sheet = Gtk::manage(new TableSheet(*this, cells));
  text_.add_child_at_anchor(*sheet, anchor);
  g_object_set_data(G_OBJECT(anchor->gobj()), "writeit-table", sheet);
  sheet->show_all();
}

void MainWindow::append_table(const Glib::RefPtr<Gtk::TextChildAnchor>& anchor,
                              std::vector<Paragraph>& out) const
{
  if (TableSheet* sheet = sheet_from(anchor))
    sheet->append(out);
}

int MainWindow::table_paragraph_count(const Glib::RefPtr<Gtk::TextChildAnchor>& anchor) const
{
  TableSheet* sheet = sheet_from(anchor);
  return sheet ? sheet->paragraph_count() : 0;
}

TableSheet* MainWindow::focused_sheet() const
{
  if (!buffer_)
    return nullptr;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    TableSheet* sheet = sheet_from(iter.get_child_anchor());
    if (sheet && sheet->contains_focus())
      return sheet;
  }
  return nullptr;
}

TableSheet* MainWindow::sheet_at_cursor() const
{
  if (!buffer_)
    return nullptr;
  return sheet_from(buffer_->get_insert()->get_iter().get_child_anchor());
}

void MainWindow::relayout_tables()
{
  if (!buffer_)
    return;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    if (TableSheet* sheet = sheet_from(iter.get_child_anchor()))
      sheet->relayout();
  }
}

void MainWindow::queue_table_sync()
{
  if (table_sync_.connected())
    return;
  table_sync_ = Glib::signal_idle().connect([this] {
    // Drop it before measuring. A resize in that measure allocates the
    // sheet again, and that has to be able to ask for another pass.
    table_sync_.disconnect();
    sync_table_lines();
    return false;
  });
}

void MainWindow::sync_table_lines()
{
  if (!buffer_)
    return;
  bool changed = false;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    auto anchor = iter.get_child_anchor();
    if (!anchor)
      continue;
    const std::vector<Gtk::Widget*> widgets = anchor->get_widgets();
    if (widgets.empty() || widgets.front() == nullptr)
      continue;
    Gtk::Widget* widget = widgets.front();
    auto* layout = static_cast<GtkTextLayout*>(
        g_object_get_data(G_OBJECT(widget->gobj()), "gtk-text-child-anchor-layout"));
    if (layout == nullptr)
      continue;
    int top = 0;
    int height = 0;
    text_.get_line_yrange(iter, top, height);
    const int want = widget->get_allocated_height();
    const int gap = want > height ? want - height : height - want;
    if (want <= 1 || gap <= 1)
      continue;
    gtk_text_child_anchor_queue_resize(anchor->gobj(), layout);
    changed = true;
  }
  if (!changed)
    return;
  // The text moved down by the table's real height. The page and the gray
  // between sheets were placed for the short line.
  grow_page_ = true;
  queue_page_size();
  queue_page_status();
}

void MainWindow::extend_table(TableSheet& sheet)
{
  // `sheet` is destroyed when the buffer is rebuilt. Read it first.
  if (cell_step_)
    on_cell_end();
  if (!buffer_ || focused_sheet() != &sheet)
    return;
  size_t index = static_cast<size_t>(std::max(0, sheet.index_in_table()));
  bool skip_sep = false;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    if (skip_sep) {
      skip_sep = false;
      if (iter.get_char() == '\n' && !iter.get_child_anchor())
        continue;
    }
    if (auto anchor = iter.get_child_anchor()) {
      if (sheet_from(anchor) == &sheet)
        break;
      TableSheet* other = sheet_from(anchor);
      index += static_cast<size_t>(other ? other->paragraph_count() : 0);
      skip_sep = true;
      continue;
    }
    if (iter.get_char() == '\n')
      ++index;
  }
  Document before = capture();
  Document after = before;
  if (index >= after.paragraphs.size() || !insert_table_row(after.paragraphs, index)) {
    text_.grab_focus();
    return;
  }
  size_t first = index;
  const int id = after.paragraphs[index].cell.table;
  while (first > 0 && after.paragraphs[first - 1].cell.table == id)
    --first;
  const int rows = after.paragraphs[index].cell.rows;
  commit_document(before, after, chars_before(after.paragraphs, first));
  if (TableSheet* next = sheet_at_cursor())
    next->focus_slot(rows - 1, 0);
}

size_t MainWindow::document_index_at(int offset) const
{
  if (!buffer_)
    return 0;
  // A focused cell wins over the body caret: the body mark stays on the
  // anchor while the keyboard is in the grid.
  if (TableSheet* focused = focused_sheet()) {
    size_t index = 0;
    bool skip_sep = false;
    for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
      if (skip_sep) {
        skip_sep = false;
        if (iter.get_char() == '\n' && !iter.get_child_anchor())
          continue;
      }
      if (auto anchor = iter.get_child_anchor()) {
        TableSheet* sheet = sheet_from(anchor);
        if (sheet == focused)
          return index + static_cast<size_t>(std::max(0, focused->index_in_table()));
        index += static_cast<size_t>(sheet ? sheet->paragraph_count() : 0);
        skip_sep = true;
        continue;
      }
      if (iter.get_char() == '\n')
        ++index;
    }
    return index + static_cast<size_t>(std::max(0, focused->index_in_table()));
  }
  size_t index = 0;
  bool skip_sep = false;
  const int count = buffer_->get_char_count();
  const int at = std::max(0, std::min(offset, count));
  const auto caret = buffer_->get_iter_at_offset(at);
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    if (skip_sep) {
      skip_sep = false;
      if (iter.get_char() == '\n' && !iter.get_child_anchor())
        continue;
    }
    if (auto anchor = iter.get_child_anchor()) {
      if (!(iter.compare(caret) < 0))
        break;
      TableSheet* sheet = sheet_from(anchor);
      index += static_cast<size_t>(sheet ? sheet->paragraph_count() : 0);
      skip_sep = true;
      continue;
    }
    if (!(iter.compare(caret) < 0))
      break;
    if (iter.get_char() == '\n')
      ++index;
  }
  return index;
}

void MainWindow::focus_table_caret()
{
  if (TableSheet* sheet = sheet_at_cursor())
    sheet->focus_slot(0, 0);
}

void MainWindow::note_cell_focus(TableSheet& sheet, int row, int column)
{
  if (loading_ || restoring_)
    return;
  if (cell_typing_on_ && (!cell_edit_.on || cell_edit_.table != sheet.table_id() ||
                          cell_edit_.row != row || cell_edit_.column != column))
    cell_typing_on_ = false;
  cell_edit_.on = true;
  cell_edit_.table = sheet.table_id();
  cell_edit_.row = row;
  cell_edit_.column = column;
  sheet.caret_of(row, column, cell_edit_.caret, cell_edit_.bound);
  // The caret is in this cell now. A highlight in another cell, or in the
  // body, is no longer where it was placed.
  if (Gtk::TextView* view = sheet.view_at(row, column))
    release_other_selections(view);
  update_actions();
  sync_format_controls();
}

void MainWindow::release_other_selections(const Gtk::TextView* keep)
{
  if (releasing_)
    return;
  releasing_ = true;
  auto collapse = [](Gtk::TextView* view) {
    if (!view)
      return;
    auto buffer = view->get_buffer();
    if (!buffer)
      return;
    Gtk::TextIter start;
    Gtk::TextIter end;
    if (!buffer->get_selection_bounds(start, end))
      return;
    buffer->place_cursor(buffer->get_insert()->get_iter());
  };
  if (keep != &text_)
    collapse(&text_);
  if (buffer_) {
    for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
      if (TableSheet* sheet = sheet_from(iter.get_child_anchor()))
        sheet->collapse_selections(keep);
    }
  }
  releasing_ = false;
}

bool MainWindow::editing_caret_on_board(int& y, int& height)
{
  Gtk::TextView* view = nullptr;
  int offset = 0;
  if (TableSheet* sheet = editing_sheet()) {
    int row = 0;
    int column = 0;
    int caret = 0;
    int bound = 0;
    if (!cell_place(*sheet, row, column, caret, bound))
      return false;
    view = sheet->view_at(row, column);
    offset = std::min(caret, bound);
  } else if (buffer_) {
    view = &text_;
    offset = buffer_->get_insert()->get_iter().get_offset();
  }
  if (!view || !view->get_realized() || !view->get_buffer())
    return false;
  auto buffer = view->get_buffer();
  const int count = buffer->get_char_count();
  offset = std::max(0, std::min(offset, count));
  Gdk::Rectangle rect;
  view->get_iter_location(buffer->get_iter_at_offset(offset), rect);
  if (rect.get_height() <= 0)
    return false;
  int wx = 0;
  int wy = 0;
  view->buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, rect.get_x(), rect.get_y(), wx, wy);
  int bx = 0;
  int by = 0;
  if (!view->translate_coordinates(board_, wx, wy, bx, by))
    return false;
  y = by;
  height = rect.get_height();
  return true;
}

void MainWindow::reveal_editing_caret()
{
  int y = 0;
  int height = 0;
  if (!editing_caret_on_board(y, height))
    return;
  auto v = paste_.get_vadjustment();
  if (!v)
    return;
  const double page = v->get_page_size();
  if (page <= 0)
    return;
  const double value = v->get_value();
  const double from = y;
  const double size = std::max(1, height);
  // Already where the style left the caret.
  if (from >= value && from + size <= value + page)
    return;
  // A style does not move the caret up the document. A coordinate still at
  // the top while the view was held further down has not been laid out yet.
  if (from + size < value - 4)
    return;
  double want = from < value ? from : from + size - page;
  const double room = std::max(v->get_lower(), v->get_upper() - page);
  want = std::max(v->get_lower(), std::min(want, room));
  if (want != value)
    v->set_value(want);
}

void MainWindow::leave_cell_edit()
{
  if (loading_ || restoring_)
    return;
  if (!cell_edit_.on && !cell_typing_on_)
    return;
  release_other_selections(&text_);
  cell_edit_.on = false;
  cell_typing_on_ = false;
  update_actions();
}

void MainWindow::note_cell_blur(TableSheet& sheet, int row, int column)
{
  if (loading_ || restoring_ || !cell_edit_.on)
    return;
  // Focus leaves this cell before the next one is entered. Refresh only the
  // caret, so a move into another cell cannot be overwritten by this one.
  if (cell_edit_.table != sheet.table_id() || cell_edit_.row != row || cell_edit_.column != column)
    return;
  sheet.caret_of(row, column, cell_edit_.caret, cell_edit_.bound);
}

void MainWindow::note_cell_marks(TableSheet& sheet, int row, int column)
{
  if (loading_ || restoring_ || cell_tagging_ || !cell_edit_.on)
    return;
  Gtk::TextView* view = sheet.view_at(row, column);
  if (!view || !view->has_focus())
    return;
  if (cell_edit_.table != sheet.table_id() || cell_edit_.row != row || cell_edit_.column != column)
    return;
  sheet.caret_of(row, column, cell_edit_.caret, cell_edit_.bound);
  // Arrow keys move the caret before the insert signal. The pending format
  // stays through that signal, and goes if the caret is still elsewhere
  // once the insert has had its turn.
  if (cell_typing_on_ &&
      (cell_edit_.caret != cell_typing_caret_ || cell_edit_.bound != cell_typing_bound_) &&
      !cell_typing_idle_.connected()) {
    cell_typing_idle_ = Glib::signal_idle().connect([this] {
      if (cell_typing_on_ && (!cell_edit_.on || cell_edit_.caret != cell_typing_caret_ ||
                              cell_edit_.bound != cell_typing_bound_))
        cell_typing_on_ = false;
      return false;
    });
  }
  sync_format_controls();
  // A selection here enables Cut, Copy, and Delete. The menu takes the
  // keyboard afterwards, and leaving the cell does not ask again.
  update_actions();
}

void MainWindow::tag_cell_insert(TableSheet& sheet, int row, int column, const Gtk::TextIter& end,
                                 const Glib::ustring& text)
{
  if (loading_ || restoring_ || text.empty() || cell_tagging_)
    return;
  auto buffer = end.get_buffer();
  if (!buffer)
    return;
  const int length = static_cast<int>(text.length());
  const int at = end.get_offset() - length;
  if (length <= 0 || at < 0)
    return;
  Run format;
  bool continued = false;
  bool from_typing = false;
  if (cell_typing_on_) {
    format = cell_typing_;
    continued = true;
    from_typing = true;
    cell_typing_on_ = false;
  } else if (at > 0) {
    auto prev = buffer->get_iter_at_offset(at - 1);
    // format_of() invents Sans 11 when a character has no format tag. Only a
    // real tag is the run to continue; otherwise the paragraph style types.
    if (prev.get_char() != '\n' && has_fmt(prev)) {
      format = format_of(prev);
      continued = true;
    }
  }
  if (!continued) {
    const Paragraph* form = sheet.form_at(row, column, sheet.line_at(row, column, at));
    format = style_typing(form ? form->style : std::string(kNormalStyle));
  }
  format.text.clear();
  format.note = 0;
  format.image.reset();
  if (!continued)
    format.direct = 0;
  auto tag = format_tag_on(buffer, format);
  if (!tag)
    return;
  struct Tagging {
    bool& flag;
    explicit Tagging(bool& flag_in)
        : flag(flag_in)
    {
      flag = true;
    }
    ~Tagging()
    {
      flag = false;
    }
  } tagging(cell_tagging_);
  buffer->apply_tag(tag, buffer->get_iter_at_offset(at), buffer->get_iter_at_offset(at + length));
  if (const Paragraph* form = sheet.form_at(row, column, sheet.line_at(row, column, at)))
    tag_cell_paragraph(buffer, at, at + length, *form);
  if (from_typing)
    show_format(format);
}

bool MainWindow::editing_cell() const
{
  return focused_sheet() != nullptr || cell_edit_.on;
}

TableSheet* MainWindow::editing_sheet() const
{
  if (TableSheet* focused = focused_sheet())
    return focused;
  if (!cell_edit_.on)
    return nullptr;
  return sheet_by_id(cell_edit_.table);
}

Gtk::TextView* MainWindow::editing_cell_view() const
{
  TableSheet* sheet = editing_sheet();
  if (!sheet)
    return nullptr;
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(*sheet, row, column, caret, bound))
    return nullptr;
  Gtk::TextView* view = sheet->view_at(row, column);
  if (!view || !view->get_buffer())
    return nullptr;
  return view;
}

TableSheet* MainWindow::sheet_by_id(int id) const
{
  if (!buffer_ || id == 0)
    return nullptr;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    TableSheet* sheet = sheet_from(iter.get_child_anchor());
    if (sheet && sheet->table_id() == id)
      return sheet;
  }
  return nullptr;
}

Gtk::TextView* MainWindow::nth_cell_view(int which, int row, int column) const
{
  if (!buffer_ || which < 0)
    return nullptr;
  int seen = 0;
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    TableSheet* sheet = sheet_from(iter.get_child_anchor());
    if (!sheet)
      continue;
    if (seen == which)
      return sheet->view_at(row, column);
    ++seen;
  }
  return nullptr;
}

bool MainWindow::cell_place(TableSheet& sheet, int& row, int& column, int& caret, int& bound) const
{
  if (sheet.focused_place(row, column, caret, bound))
    return true;
  if (!cell_edit_.on || cell_edit_.table != sheet.table_id())
    return false;
  row = cell_edit_.row;
  column = cell_edit_.column;
  caret = cell_edit_.caret;
  bound = cell_edit_.bound;
  return sheet.view_at(row, column) != nullptr;
}

size_t MainWindow::cell_paragraph_index(const TableSheet& sheet, int row, int column,
                                        int line) const
{
  size_t index = 0;
  bool skip_sep = false;
  bool found = false;
  if (buffer_) {
    for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
      if (skip_sep) {
        skip_sep = false;
        if (iter.get_char() == '\n' && !iter.get_child_anchor())
          continue;
      }
      if (auto anchor = iter.get_child_anchor()) {
        TableSheet* other = sheet_from(anchor);
        if (other == &sheet) {
          found = true;
          break;
        }
        index += static_cast<size_t>(other ? other->paragraph_count() : 0);
        skip_sep = true;
        continue;
      }
      if (iter.get_char() == '\n')
        ++index;
    }
  }
  if (!found)
    return static_cast<size_t>(-1);
  return index + static_cast<size_t>(sheet.paragraphs_before(row, column) + std::max(0, line));
}

void MainWindow::toggle_cell_flag(TextFlag flag)
{
  TableSheet* sheet = editing_sheet();
  if (!sheet) {
    restore_edit_focus();
    return;
  }
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(*sheet, row, column, caret, bound)) {
    restore_edit_focus();
    return;
  }
  Gtk::TextView* view = sheet->view_at(row, column);
  if (!view || !view->get_buffer()) {
    restore_edit_focus();
    return;
  }
  auto buffer = view->get_buffer();
  Gtk::TextBuffer::iterator sel_from;
  Gtk::TextBuffer::iterator sel_to;
  if (buffer->get_selection_bounds(sel_from, sel_to)) {
    const int live_lo = std::min(sel_from.get_offset(), sel_to.get_offset());
    const int live_hi = std::max(sel_from.get_offset(), sel_to.get_offset());
    const int saved_lo = std::min(caret, bound);
    const int saved_hi = std::max(caret, bound);
    if (live_hi - live_lo > saved_hi - saved_lo) {
      caret = sel_from.get_offset();
      bound = sel_to.get_offset();
    }
  }
  const int start = std::min(caret, bound);
  const int end = std::max(caret, bound);
  CellEdit saved;
  saved.on = true;
  saved.table = sheet->table_id();
  saved.row = row;
  saved.column = column;
  saved.caret = caret;
  saved.bound = bound;

  if (start == end) {
    Run format;
    if (cell_typing_on_ && cell_typing_caret_ == start && cell_typing_bound_ == end)
      format = cell_typing_;
    else {
      const int count = buffer->get_char_count();
      const int at = std::max(0, std::min(start, count));
      bool have = false;
      if (at > 0) {
        auto prev = buffer->get_iter_at_offset(at - 1);
        if (prev.get_char() != '\n' && has_fmt(prev)) {
          format = format_of(prev);
          have = true;
        }
      }
      if (!have && at < count) {
        auto here = buffer->get_iter_at_offset(at);
        if (here.get_char() != '\n' && has_fmt(here)) {
          format = format_of(here);
          have = true;
        }
      }
      if (!have) {
        const Paragraph* form = sheet->form_at(row, column, sheet->line_at(row, column, at));
        format = style_typing(form ? form->style : std::string(kNormalStyle));
      }
    }
    format.text.clear();
    format.note = 0;
    format.image.reset();
    set_text_flag(format, flag, !text_flag(format, flag));
    cell_typing_ = format;
    cell_typing_on_ = true;
    cell_typing_caret_ = start;
    cell_typing_bound_ = end;
    show_format(format);
    restore_cell_edit(saved);
    return;
  }

  cell_typing_on_ = false;
  bool all = true;
  bool any = false;
  for (int i = start; i < end; ++i) {
    auto iter = buffer->get_iter_at_offset(i);
    if (iter.get_char() == '\n' && !iter.starts_line())
      continue;
    any = true;
    if (!text_flag(format_of(iter), flag)) {
      all = false;
      break;
    }
  }
  if (!any) {
    restore_cell_edit(saved);
    return;
  }
  const bool turn_on = !all;
  buffer->begin_user_action();
  for (int i = start; i < end;) {
    auto iter = buffer->get_iter_at_offset(i);
    if (iter.get_char() == '\n') {
      if (i == 0 || buffer->get_iter_at_offset(i - 1).get_char() == '\n') {
        Run mark = format_of(iter);
        set_text_flag(mark, flag, turn_on);
        set_fmt_on(buffer, iter, buffer->get_iter_at_offset(i + 1), format_tag_on(buffer, mark));
      }
      ++i;
      continue;
    }
    Run run = format_of(iter);
    int j = i + 1;
    while (j < end) {
      auto next = buffer->get_iter_at_offset(j);
      if (next.get_char() == '\n' || !same_format(format_of(next), run))
        break;
      ++j;
    }
    set_text_flag(run, flag, turn_on);
    set_fmt_on(buffer, buffer->get_iter_at_offset(i), buffer->get_iter_at_offset(j),
               format_tag_on(buffer, run));
    i = j;
  }
  buffer->end_user_action();
  restore_cell_edit(saved);
}

void MainWindow::edit_cell_runs(const std::function<void(Run&)>& edit)
{
  TableSheet* sheet = editing_sheet();
  if (!sheet) {
    restore_edit_focus();
    return;
  }
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(*sheet, row, column, caret, bound)) {
    restore_edit_focus();
    return;
  }
  Gtk::TextView* view = sheet->view_at(row, column);
  if (!view || !view->get_buffer()) {
    restore_edit_focus();
    return;
  }
  auto buffer = view->get_buffer();
  Gtk::TextBuffer::iterator sel_from;
  Gtk::TextBuffer::iterator sel_to;
  if (buffer->get_selection_bounds(sel_from, sel_to)) {
    const int live_lo = std::min(sel_from.get_offset(), sel_to.get_offset());
    const int live_hi = std::max(sel_from.get_offset(), sel_to.get_offset());
    const int saved_lo = std::min(caret, bound);
    const int saved_hi = std::max(caret, bound);
    if (live_hi - live_lo > saved_hi - saved_lo) {
      caret = sel_from.get_offset();
      bound = sel_to.get_offset();
    }
  }
  const int start = std::min(caret, bound);
  const int end = std::max(caret, bound);
  CellEdit saved;
  saved.on = true;
  saved.table = sheet->table_id();
  saved.row = row;
  saved.column = column;
  saved.caret = caret;
  saved.bound = bound;

  if (start == end) {
    Run format;
    if (cell_typing_on_ && cell_typing_caret_ == start && cell_typing_bound_ == end)
      format = cell_typing_;
    else {
      const int count = buffer->get_char_count();
      const int at = std::max(0, std::min(start, count));
      bool have = false;
      if (at > 0) {
        auto prev = buffer->get_iter_at_offset(at - 1);
        if (prev.get_char() != '\n' && has_fmt(prev)) {
          format = format_of(prev);
          have = true;
        }
      }
      if (!have && at < count) {
        auto here = buffer->get_iter_at_offset(at);
        if (here.get_char() != '\n' && has_fmt(here)) {
          format = format_of(here);
          have = true;
        }
      }
      if (!have) {
        const Paragraph* form = sheet->form_at(row, column, sheet->line_at(row, column, at));
        format = style_typing(form ? form->style : std::string(kNormalStyle));
      }
    }
    format.text.clear();
    format.note = 0;
    format.image.reset();
    edit(format);
    cell_typing_ = format;
    cell_typing_on_ = true;
    cell_typing_caret_ = start;
    cell_typing_bound_ = end;
    show_format(format);
    restore_cell_edit(saved);
    return;
  }

  cell_typing_on_ = false;
  bool any = false;
  for (int i = start; i < end; ++i) {
    auto iter = buffer->get_iter_at_offset(i);
    if (iter.get_char() == '\n' && !iter.starts_line())
      continue;
    any = true;
    break;
  }
  if (!any) {
    restore_cell_edit(saved);
    return;
  }
  buffer->begin_user_action();
  for (int i = start; i < end;) {
    auto iter = buffer->get_iter_at_offset(i);
    if (iter.get_char() == '\n') {
      if (i == 0 || buffer->get_iter_at_offset(i - 1).get_char() == '\n') {
        Run mark = format_of(iter);
        edit(mark);
        set_fmt_on(buffer, iter, buffer->get_iter_at_offset(i + 1), format_tag_on(buffer, mark));
      }
      ++i;
      continue;
    }
    Run run = format_of(iter);
    int j = i + 1;
    while (j < end) {
      auto next = buffer->get_iter_at_offset(j);
      if (next.get_char() == '\n' || !same_format(format_of(next), run))
        break;
      ++j;
    }
    edit(run);
    set_fmt_on(buffer, buffer->get_iter_at_offset(i), buffer->get_iter_at_offset(j),
               format_tag_on(buffer, run));
    i = j;
  }
  buffer->end_user_action();
  restore_cell_edit(saved);
}

void MainWindow::apply_cell_style(TableSheet& sheet, const std::string& name)
{
  cell_typing_on_ = false;
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(sheet, row, column, caret, bound)) {
    show_style();
    restore_edit_focus();
    return;
  }
  Gtk::TextView* view = sheet.view_at(row, column);
  if (!view || !view->get_buffer()) {
    show_style();
    restore_edit_focus();
    return;
  }
  auto buffer = view->get_buffer();
  Gtk::TextBuffer::iterator sel_from;
  Gtk::TextBuffer::iterator sel_to;
  if (buffer->get_selection_bounds(sel_from, sel_to)) {
    const int live_lo = std::min(sel_from.get_offset(), sel_to.get_offset());
    const int live_hi = std::max(sel_from.get_offset(), sel_to.get_offset());
    const int saved_lo = std::min(caret, bound);
    const int saved_hi = std::max(caret, bound);
    // A collapsed PRIMARY selection must not throw away the range the cell had.
    if (live_hi - live_lo > saved_hi - saved_lo) {
      caret = sel_from.get_offset();
      bound = sel_to.get_offset();
    }
  }
  const int start = std::min(caret, bound);
  const int end = std::max(caret, bound);
  const int count = buffer->get_char_count();
  const int from = std::max(0, std::min(start, count));
  const int to = std::max(0, std::min(end, count));
  const int first_line = sheet.line_at(row, column, from);
  const int last_line = sheet.line_at(row, column, to > from ? to - 1 : to);

  CellEdit saved;
  saved.on = true;
  saved.table = sheet.table_id();
  saved.row = row;
  saved.column = column;
  saved.caret = caret;
  saved.bound = bound;
  auto keep = [&]() {
    show_style();
    restore_cell_edit(saved);
  };

  const Document before = capture();
  Document after = before;
  adopt_sheet(after, typing_.font, typing_.size);
  const std::vector<Style> styles = complete_sheet(style_sheet(after));
  const Style* to_style = find_style(styles, name);
  if (!to_style) {
    keep();
    return;
  }
  bool any = false;
  for (int line = first_line; line <= last_line; ++line) {
    const size_t index = cell_paragraph_index(sheet, row, column, line);
    if (index >= after.paragraphs.size())
      continue;
    Paragraph& paragraph = after.paragraphs[index];
    if (paragraph.cell.table == 0 || paragraph.cell.table != sheet.table_id())
      continue;
    if (paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    take_paragraph_style(paragraph, *to_style);
    any = true;
  }
  if (!any || after.paragraphs == before.paragraphs) {
    keep();
    return;
  }
  const std::string applied = to_style->name;
  // commit_document() rebuilds the body and follows its insert, which is
  // often still the first line while the caret is in this cell.
  hold_view();
  commit_document(before, after);
  typing_ = style_typing(applied);
  restore_cell_edit(saved);
  release_view(false);
}

void MainWindow::show_cell_style()
{
  TableSheet* sheet = editing_sheet();
  if (!sheet)
    return;
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(*sheet, row, column, caret, bound))
    return;
  const int start = std::min(caret, bound);
  const int end = std::max(caret, bound);
  const int first = sheet->line_at(row, column, start);
  const int last = sheet->line_at(row, column, end > start ? end - 1 : end);
  const std::vector<Style> styles = this->sheet();
  auto style_of = [&](int line) -> const Style* {
    const Paragraph* form = sheet->form_at(row, column, line);
    const Style* style = form ? find_style(styles, form->style) : nullptr;
    return style ? style : (styles.empty() ? nullptr : &styles.front());
  };
  const Style* style = style_of(first);
  for (int line = first; style && line <= last; ++line) {
    if (style_of(line) != style)
      style = nullptr;
  }
  const bool guard = suppress_format_;
  suppress_format_ = true;
  if (!style) {
    if (style_combo_.get_active_row_number() != -1)
      style_combo_.set_active(-1);
  } else if (style_combo_.get_active_text() != style->name) {
    style_combo_.set_active_text(style->name);
  }
  suppress_format_ = guard;
}

void MainWindow::show_cell_format()
{
  show_cell_style();
  TableSheet* sheet = editing_sheet();
  if (!sheet)
    return;
  int row = 0;
  int column = 0;
  int caret = 0;
  int bound = 0;
  if (!cell_place(*sheet, row, column, caret, bound))
    return;
  Gtk::TextView* view = sheet->view_at(row, column);
  if (!view || !view->get_buffer())
    return;
  auto buffer = view->get_buffer();
  const int count = buffer->get_char_count();
  caret = std::max(0, std::min(caret, count));
  bound = std::max(0, std::min(bound, count));
  if (cell_typing_on_ && caret == cell_typing_caret_ && bound == cell_typing_bound_) {
    Run pending = cell_typing_;
    pending.text.clear();
    show_format(pending);
    return;
  }
  Run run;
  bool have = false;
  if (caret > 0) {
    auto prev = buffer->get_iter_at_offset(caret - 1);
    if (prev.get_char() != '\n' && has_fmt(prev)) {
      run = format_of(prev);
      have = true;
    }
  }
  if (!have && caret < count) {
    auto at = buffer->get_iter_at_offset(caret);
    if (at.get_char() != '\n' && has_fmt(at)) {
      run = format_of(at);
      have = true;
    }
  }
  if (!have) {
    const Paragraph* form = sheet->form_at(row, column, sheet->line_at(row, column, caret));
    run = style_typing(form ? form->style : std::string(kNormalStyle));
  }
  run.text.clear();
  show_format(run);
}

void MainWindow::restore_edit_focus()
{
  if (cell_edit_.on) {
    if (TableSheet* sheet = sheet_by_id(cell_edit_.table)) {
      sheet->focus_slot(cell_edit_.row, cell_edit_.column);
      return;
    }
  }
  text_.grab_focus();
}

void MainWindow::restore_cell_edit(const CellEdit& saved)
{
  if (!saved.on) {
    cell_edit_.on = false;
    return;
  }
  TableSheet* sheet = sheet_by_id(saved.table);
  Gtk::TextView* view = sheet ? sheet->view_at(saved.row, saved.column) : nullptr;
  if (!sheet || !view || !view->get_buffer()) {
    cell_edit_.on = false;
    text_.grab_focus();
    return;
  }
  cell_edit_ = saved;
  sheet->focus_slot(saved.row, saved.column);
  auto buffer = view->get_buffer();
  const int chars = buffer->get_char_count();
  const int caret = std::max(0, std::min(saved.caret, chars));
  const int bound = std::max(0, std::min(saved.bound, chars));
  buffer->select_range(buffer->get_iter_at_offset(caret), buffer->get_iter_at_offset(bound));
  cell_edit_.on = true;
  cell_edit_.caret = caret;
  cell_edit_.bound = bound;
  sync_format_controls();
}

void MainWindow::apply_document_page(PageSetup page)
{
  page = clamp_page(page);
  if (page_setup_ == page)
    return;
  page_setup_ = page;
  // Margins live in the paragraph tags. Same zoom, new paper: restyle them.
  styled_zoom_ = -1;
  apply_page_size();
  ruler_.queue_draw();
}

void MainWindow::install_stories(const Document& doc)
{
  if (!(page_setup_ == clamp_page(doc.page)))
    apply_document_page(doc.page);
  else
    page_setup_ = clamp_page(doc.page);
  header_ = doc.header;
  footer_ = doc.footer;
  notes_ = doc.notes;
  refresh_stories();
}

void MainWindow::queue_note_numbers()
{
  if (note_numbers_idle_.connected())
    return;
  note_numbers_idle_ = Glib::signal_idle().connect([this] {
    // Cleared first, so a resize during the align can ask for another pass.
    note_numbers_idle_.disconnect();
    align_note_numbers();
    return false;
  });
}

void MainWindow::align_note_view(Gtk::TextView& view, Gtk::Fixed& marks, Gtk::Label& first_label,
                                 std::vector<Gtk::Label*>& labels, std::vector<int>& ys,
                                 const std::vector<int>& numbers)
{
  auto buffer = view.get_buffer();
  if (!buffer)
    return;
  // The same split notes_from_text uses. One newline is another paragraph of
  // this note. Two newlines start the next note, so its number follows that
  // line rather than the source line the number used to occupy.
  const Glib::ustring text = buffer->get_text();
  std::vector<int> starts;
  if (!text.empty() || !notes_.empty()) {
    starts.push_back(0);
    for (int i = 0; i + 1 < static_cast<int>(text.size()); ++i) {
      if (text[static_cast<size_t>(i)] != '\n' || text[static_cast<size_t>(i + 1)] != '\n')
        continue;
      if (starts.size() >= static_cast<size_t>(kMaxNotes))
        break;
      starts.push_back(i + 2);
      ++i;
    }
  }

  if (labels.empty())
    labels.push_back(&first_label);
  while (labels.size() < starts.size()) {
    auto* label = Gtk::manage(new Gtk::Label());
    label->set_halign(Gtk::ALIGN_START);
    label->set_valign(Gtk::ALIGN_START);
    marks.put(*label, 0, 0);
    labels.push_back(label);
  }
  if (ys.size() < labels.size())
    ys.resize(labels.size(), -1);

  if (starts.empty()) {
    first_label.set_text("");
    for (Gtk::Label* label : labels) {
      if (label)
        label->hide();
    }
    return;
  }

  const int chars = buffer->get_char_count();
  auto y_at = [&](int offset) {
    offset = std::max(0, std::min(offset, chars));
    Gdk::Rectangle loc;
    view.get_iter_location(buffer->get_iter_at_offset(offset), loc);
    int wx = 0;
    int wy = 0;
    view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, loc.get_x(), loc.get_y(), wx, wy);
    return std::max(0, wy);
  };

  for (size_t i = 0; i < labels.size(); ++i) {
    Gtk::Label* label = labels[i];
    if (!label)
      continue;
    if (i >= starts.size()) {
      label->hide();
      if (label->get_parent() == &marks && ys[i] != 0) {
        marks.move(*label, 0, 0);
        ys[i] = 0;
      }
      continue;
    }
    int number = static_cast<int>(i) + 1;
    if (!numbers.empty()) {
      if (i < numbers.size())
        number = numbers[i];
      else
        number = numbers.back() + static_cast<int>(i - numbers.size()) + 1;
    }
    const std::string shown = std::to_string(number);
    if (label->get_text() != shown)
      label->set_text(shown);
    const int y = y_at(starts[i]);
    if (label->get_parent() != &marks)
      marks.put(*label, 0, y);
    else if (ys[i] != y)
      marks.move(*label, 0, y);
    ys[i] = y;
    label->show();
  }
  marks.show();
}

void MainWindow::align_note_numbers()
{
  if (note_groups_.size() <= 1) {
    align_note_view(notes_view_, note_marks_, note_label_, note_numbers_, note_number_y_, {});
    return;
  }
  for (size_t g = 0; g < note_groups_.size(); ++g) {
    std::vector<int> numbers;
    for (int index : note_groups_[g].indexes)
      numbers.push_back(index + 1);
    if (g == 0) {
      align_note_view(notes_view_, note_marks_, note_label_, note_numbers_, note_number_y_,
                      numbers);
      continue;
    }
    if (g - 1 >= note_bands_.size() || !note_bands_[g - 1])
      continue;
    NoteBand& band = *note_bands_[g - 1];
    align_note_view(band.view, band.marks, band.label, band.numbers, band.number_y, numbers);
  }
}

void MainWindow::refresh_stories()
{
  const bool has = story_present(header_) || story_present(footer_);
  if (has && !stories_chosen_ && !stories_on_ && header_footer_item_) {
    suppress_stories_ = true;
    header_footer_item_->set_active(true);
    stories_on_ = true;
    suppress_stories_ = false;
  }
  auto fill = [this](Gtk::TextView& view, const std::string& text, std::string& shown) {
    auto buffer = view.get_buffer();
    // The keystroke is already in the view. Replacing the whole buffer
    // collapses the note and grows it again, so the page jumps twice.
    if (buffer && buffer->get_text().raw() == text) {
      shown = text;
      return;
    }
    const bool was = loading_;
    loading_ = true;
    view.get_buffer()->set_text(text);
    shown = text;
    loading_ = was;
  };
  fill(header_view_, story_text(header_), header_shown_);
  fill(footer_view_, story_text(footer_), footer_shown_);
  // A rebuild can arrive with groups from the document that was just
  // replaced. Those indexes are not this one's notes. Show the whole column
  // until the next seam pass splits it again.
  if (note_groups_.size() > 1) {
    std::vector<char> seen(notes_.size(), 0);
    bool cover = true;
    for (const NoteGroup& group : note_groups_) {
      for (int index : group.indexes) {
        if (index < 0 || index >= static_cast<int>(seen.size()) || seen[static_cast<size_t>(index)])
          cover = false;
        else
          seen[static_cast<size_t>(index)] = 1;
      }
    }
    if (cover) {
      for (char bit : seen) {
        if (!bit)
          cover = false;
      }
    }
    if (!cover)
      note_groups_.clear();
  }
  if (note_groups_.size() <= 1) {
    fill(notes_view_, notes_text(notes_), notes_shown_);
  } else {
    for (size_t i = 0; i < note_groups_.size(); ++i) {
      Gtk::TextView& view = i == 0 ? notes_view_ : ensure_note_band(i - 1).view;
      std::string extra;
      fill(view, note_group_text(note_groups_[i]), i == 0 ? notes_shown_ : extra);
    }
  }
  if (reveal_note_ > static_cast<int>(notes_.size()))
    reveal_note_ = 0;
  align_note_numbers();
  queue_note_numbers();

  const bool page = view_ == ViewMode::Page;
  const bool show_frames = page && stories_on_;
  auto attach = [this](Gtk::TextView& view, bool on) {
    if (!on) {
      if (view.get_parent() == &page_box_) {
        view.hide();
        page_box_.remove(view);
      }
      return;
    }
    if (view.get_parent() != &page_box_)
      page_box_.pack_start(view, Gtk::PACK_SHRINK);
    view.show();
  };
  attach(header_view_, show_frames);
  attach(footer_view_, show_frames);
  int pos = 0;
  auto order = [this, &pos](Gtk::Widget& widget) {
    if (widget.get_parent() == &page_box_)
      page_box_.reorder_child(widget, pos++);
  };
  order(header_view_);
  order(text_);
  order(footer_view_);
  place_notes();
  if (!loading_)
    apply_page_size();
}

int MainWindow::note_block_px() const
{
  if (notes_.empty() || view_ != ViewMode::Page)
    return 0;
  int min_h = 0;
  int nat_h = 0;
  note_box_.get_preferred_height(min_h, nat_h);
  // Mid-key the text view's layout is invalid and this height is one line,
  // or nothing. The band already on the page is the real one. A taller
  // measurement is a new line, and that one is applied.
  if (note_editing_ && note_h_ > 1 && nat_h < note_h_)
    return note_h_;
  if (nat_h > 1)
    return nat_h;
  return std::max(1, static_cast<int>(std::ceil(18 * std::max(0.5, zoom_factor()))));
}

std::string MainWindow::note_group_text(const NoteGroup& group) const
{
  std::vector<std::vector<Paragraph>> slice;
  for (int index : group.indexes) {
    if (index < 0 || index >= static_cast<int>(notes_.size()))
      continue;
    slice.push_back(notes_[static_cast<size_t>(index)]);
  }
  return notes_text(slice);
}

void MainWindow::show_note_text(Gtk::TextView& view, const std::string& text)
{
  auto buffer = view.get_buffer();
  if (!buffer || buffer->get_text().raw() == text)
    return;
  // set_text ends a user action. That would commit the slice as a new edit
  // while a seam pass is only showing it.
  const bool was = loading_;
  loading_ = true;
  buffer->set_text(text);
  if (&view == &notes_view_)
    notes_shown_ = text;
  loading_ = was;
}

namespace {

int note_line_y(Gtk::TextView& view, const Glib::RefPtr<Gtk::TextBuffer>& buffer, int offset)
{
  const int chars = buffer->get_char_count();
  offset = std::max(0, std::min(offset, chars));
  Gdk::Rectangle loc;
  view.get_iter_location(buffer->get_iter_at_offset(offset), loc);
  int wx = 0;
  int wy = 0;
  view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, loc.get_x(), loc.get_y(), wx, wy);
  return std::max(0, wy);
}

}  // namespace

int MainWindow::measure_note_group(const NoteGroup& group)
{
  if (group.indexes.empty())
    return 0;
  auto buffer = notes_view_.get_buffer();
  const std::string full = notes_text(notes_);
  if (buffer && buffer->get_text().raw() == full) {
    std::vector<int> starts;
    starts.push_back(0);
    for (int i = 0; i + 1 < static_cast<int>(full.size()); ++i) {
      if (full[static_cast<size_t>(i)] != '\n' || full[static_cast<size_t>(i + 1)] != '\n')
        continue;
      starts.push_back(i + 2);
      ++i;
      if (starts.size() >= notes_.size())
        break;
    }
    auto piece = [&](int index) {
      if (index < 0 || index >= static_cast<int>(starts.size()))
        return 0;
      const int y0 = note_line_y(notes_view_, buffer, starts[static_cast<size_t>(index)]);
      // The next note starts after a blank line. That line's top is the
      // bottom of this note. The last note has no blank after it, so a
      // one-line note reports the same y at both ends: use the line's height.
      int end_at = buffer->get_char_count();
      if (index + 1 < static_cast<int>(starts.size()))
        end_at = std::max(0, starts[static_cast<size_t>(index + 1)] - 1);
      const int y1 = note_line_y(notes_view_, buffer, end_at);
      if (y1 > y0)
        return y1 - y0;
      Gdk::Rectangle loc;
      const int chars = buffer->get_char_count();
      const int at = std::max(0, std::min(starts[static_cast<size_t>(index)], chars));
      notes_view_.get_iter_location(buffer->get_iter_at_offset(at), loc);
      return std::max(0, loc.get_height());
    };
    int blank = 0;
    if (starts.size() >= 2) {
      const int at = std::max(0, starts[1] - 1);
      blank = std::max(
          0, note_line_y(notes_view_, buffer, starts[1]) - note_line_y(notes_view_, buffer, at));
    }
    int height = 0;
    bool any = false;
    for (size_t k = 0; k < group.indexes.size(); ++k) {
      const int part = piece(group.indexes[k]);
      if (part <= 0)
        continue;
      if (any)
        height += blank;
      height += part;
      any = true;
    }
    if (height > 1)
      return height;
  }

  auto held = [&](Gtk::Widget& box, int remembered) {
    int min_h = 0;
    int nat_h = 0;
    box.get_preferred_height(min_h, nat_h);
    if (note_editing_ && remembered > 1 && nat_h < remembered)
      return remembered;
    return nat_h > 1 ? nat_h : 0;
  };
  if (!note_groups_.empty() && group.indexes == note_groups_.front().indexes) {
    const int drawn = held(note_box_, note_h_);
    if (drawn > 1)
      return drawn;
  }
  for (size_t i = 1; i < note_groups_.size() && i - 1 < note_bands_.size(); ++i) {
    if (!note_bands_[i - 1] || note_groups_[i].indexes != group.indexes)
      continue;
    const int drawn = held(note_bands_[i - 1]->box, note_bands_[i - 1]->h);
    if (drawn > 1)
      return drawn;
  }

  const std::string text = note_group_text(group);
  auto layout =
      notes_view_.create_pango_layout(text.empty() ? Glib::ustring(" ") : Glib::ustring(text));
  const ViewGeometry sheet = geometry();
  const int width =
      std::max(40, text_.get_allocated_width() - sheet.margin_left - sheet.margin_right);
  const int inner = std::max(40, width - 36);
  layout->set_width(inner * PANGO_SCALE);
  layout->set_wrap(Pango::WRAP_WORD_CHAR);
  int layout_w = 0;
  int layout_h = 0;
  layout->get_pixel_size(layout_w, layout_h);
  return std::max(1, layout_h);
}

bool MainWindow::note_overlay_box(GtkWidget* widget, int& x, int& y, int& w, int& h) const
{
  if (widget == GTK_WIDGET(note_box_.gobj())) {
    x = note_x_;
    y = note_y_;
    w = note_w_;
    h = note_h_;
    return true;
  }
  for (const auto& band : note_bands_) {
    if (!band || widget != GTK_WIDGET(band->box.gobj()))
      continue;
    x = band->x;
    y = band->y;
    w = band->w;
    h = band->h;
    return true;
  }
  return false;
}

bool MainWindow::is_note_view(const Gtk::Widget* widget) const
{
  if (widget == &notes_view_)
    return true;
  for (const auto& band : note_bands_) {
    if (band && widget == &band->view)
      return true;
  }
  return false;
}

Gtk::TextView* MainWindow::note_view_for(Gtk::Widget* widget)
{
  if (widget == &notes_view_)
    return &notes_view_;
  for (auto& band : note_bands_) {
    if (band && widget == &band->view)
      return &band->view;
  }
  return nullptr;
}

Gtk::TextView* MainWindow::view_for_note(int number)
{
  if (note_groups_.size() <= 1 || number <= 0)
    return &notes_view_;
  const int index = number - 1;
  for (size_t g = 0; g < note_groups_.size(); ++g) {
    const auto& indexes = note_groups_[g].indexes;
    if (std::find(indexes.begin(), indexes.end(), index) == indexes.end())
      continue;
    if (g == 0)
      return &notes_view_;
    if (g - 1 < note_bands_.size() && note_bands_[g - 1])
      return &note_bands_[g - 1]->view;
  }
  return &notes_view_;
}

Gtk::Box* MainWindow::box_for_note(int number)
{
  if (note_groups_.size() <= 1 || number <= 0)
    return &note_box_;
  const int index = number - 1;
  for (size_t g = 0; g < note_groups_.size(); ++g) {
    const auto& indexes = note_groups_[g].indexes;
    if (std::find(indexes.begin(), indexes.end(), index) == indexes.end())
      continue;
    if (g == 0)
      return &note_box_;
    if (g - 1 < note_bands_.size() && note_bands_[g - 1])
      return &note_bands_[g - 1]->box;
  }
  return &note_box_;
}

MainWindow::NoteBand& MainWindow::ensure_note_band(size_t index)
{
  while (note_bands_.size() <= index)
    note_bands_.push_back(std::make_unique<NoteBand>());
  NoteBand& band = *note_bands_[index];
  if (!band.wired)
    wire_note_band(band);
  return band;
}

void MainWindow::wire_note_band(NoteBand& band)
{
  band.view.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  band.view.set_hexpand(true);
  band.view.set_left_margin(8);
  band.view.set_right_margin(8);
  band.view.get_style_context()->add_class("page-text");
  band.view.set_no_show_all(false);
  band.label.set_halign(Gtk::ALIGN_START);
  band.label.set_valign(Gtk::ALIGN_START);
  band.marks.put(band.label, 0, 0);
  band.marks.set_margin_right(6);
  band.marks.set_halign(Gtk::ALIGN_START);
  band.marks.set_valign(Gtk::ALIGN_START);
  band.box.pack_start(band.marks, Gtk::PACK_SHRINK);
  band.box.pack_start(band.view, Gtk::PACK_EXPAND_WIDGET);
  band.box.set_halign(Gtk::ALIGN_START);
  band.box.set_valign(Gtk::ALIGN_START);
  band.box.set_no_show_all(true);
  band.box.hide();
  page_overlay_.add_overlay(band.box);
  band.size_conn =
      band.view.signal_size_allocate().connect([this](Gtk::Allocation&) { queue_note_numbers(); });
  band.focus_in_conn = band.view.signal_focus_in_event().connect([this](GdkEventFocus*) {
    note_editing_ = true;
    return false;
  });
  band.focus_out_conn = band.view.signal_focus_out_event().connect([this, &band](GdkEventFocus*) {
    note_editing_ = false;
    release_note_scroll(false);
    if (!loading_ && band.view.get_parent() != nullptr)
      place_notes();
    return false;
  });
  band.end_conn = band.view.get_buffer()->signal_end_user_action().connect(
      [this, &band] { on_note_view_end(band.view); });
  band.key_conn =
      band.view.signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_story_key));
  band.wired = true;
}

void MainWindow::reveal_note_view()
{
  Gtk::Box* box = box_for_note(reveal_note_);
  if (!box)
    box = &note_box_;
  if (!box->get_visible() || box->get_parent() == nullptr)
    return;
  int x = 0;
  int y = 0;
  if (!box->translate_coordinates(board_, 0, 0, x, y))
    return;
  auto adj = paste_.get_vadjustment();
  if (!adj)
    return;
  const double page = adj->get_page_size();
  if (page <= 0)
    return;
  const double from = y;
  const double size = std::max(1, box->get_allocated_height());
  const double value = adj->get_value();
  // Taller than the window: the top and the bottom cannot both be on screen.
  // Asking for one and then the other alternates on each layout, so the page
  // bounces under the line being typed. Bring the top in only when the note
  // is off screen. A note that fits still slides in whole, with a little room.
  const bool fits = page >= size + 36.0;
  const double pad = fits ? 18.0 : 0.0;
  double want = value;
  if (!fits) {
    if (from >= value + page || from + size <= value)
      want = from;
  } else if (from - pad < value) {
    want = from - pad;
  } else if (from + size + pad > value + page) {
    want = from + size + pad - page;
  }
  want = std::max(adj->get_lower(), std::min(want, adj->get_upper() - page));
  if (want != value)
    adj->set_value(want);
}

void MainWindow::reveal_story_caret(Gtk::TextView& view)
{
  auto buffer = view.get_buffer();
  if (!buffer || !view.get_realized() || !view.get_visible() || view.get_allocated_height() <= 1)
    return;
  const Gtk::TextIter caret = buffer->get_insert()->get_iter();
  Gdk::Rectangle rect;
  view.get_iter_location(caret, rect);
  if (rect.get_height() <= 0)
    return;
  // Before the lines are laid out every offset is reported at y 0. Scrolling
  // there jumps the page to the top for one key, then back on the next.
  if (rect.get_y() == 0 && caret.get_offset() > 0) {
    Gdk::Rectangle end_rect;
    view.get_iter_location(buffer->end(), end_rect);
    if (end_rect.get_y() == 0)
      return;
  }
  int wx = 0;
  int wy = 0;
  view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, rect.get_x(), rect.get_y(), wx, wy);
  int x = 0;
  int y = 0;
  if (!view.translate_coordinates(board_, wx, wy, x, y))
    return;
  auto adj = paste_.get_vadjustment();
  if (!adj)
    return;
  const double page = adj->get_page_size();
  if (page <= 0)
    return;
  const double from = y;
  const double size = std::max(1, rect.get_height());
  const double value = adj->get_value();
  const double pad = page >= size + 36.0 ? 18.0 : 0.0;
  double want = value;
  if (from - pad < value)
    want = from - pad;
  else if (from + size + pad > value + page)
    want = from + size + pad - page;
  want = std::max(adj->get_lower(), std::min(want, adj->get_upper() - page));
  if (want != value)
    adj->set_value(want);
}

StoryKey story_key(unsigned keyval, unsigned state)
{
  const guint mods = static_cast<guint>(state) & gtk_accelerator_get_default_mod_mask();
  if ((mods & GDK_CONTROL_MASK) == 0)
    return StoryKey::Pass;
  const bool shift = (mods & GDK_SHIFT_MASK) != 0;
  if (keyval == GDK_KEY_z && !shift)
    return StoryKey::Undo;
  if (keyval == GDK_KEY_y || (keyval == GDK_KEY_z && shift) || keyval == GDK_KEY_Z)
    return StoryKey::Redo;
  return StoryKey::Pass;
}

bool MainWindow::on_story_key(GdkEventKey* event)
{
  if (!event)
    return false;
  switch (story_key(event->keyval, event->state)) {
    case StoryKey::Undo:
      undo();
      return true;
    case StoryKey::Redo:
      redo();
      return true;
    case StoryKey::Pass:
      return false;
  }
  return false;
}

void MainWindow::on_header_footer()
{
  if (suppress_stories_)
    return;
  stories_chosen_ = true;
  stories_on_ = header_footer_item_ && header_footer_item_->get_active();
  refresh_stories();
}

void MainWindow::on_story_end(std::vector<Paragraph>* story)
{
  if (loading_ || restoring_ || !story || !buffer_)
    return;
  Gtk::TextView& view = story == &header_ ? header_view_ : footer_view_;
  std::string& shown = story == &header_ ? header_shown_ : footer_shown_;
  const std::string text = view.get_buffer()->get_text().raw();
  if (text == story_text(*story))
    return;
  Document before = capture();
  Document after = before;
  std::vector<Paragraph> next;
  if (!blank_text(text))
    next = paragraphs_from_lines(text, typing_);
  if (story == &header_)
    after.header = next;
  else
    after.footer = next;
  Gtk::Widget* focused = get_focus();
  if (blank_text(text)) {
    const bool was = loading_;
    loading_ = true;
    view.get_buffer()->set_text("");
    shown = "";
    loading_ = was;
  }
  commit_document(before, after);
  if (focused == &header_view_ || focused == &footer_view_ || is_note_view(focused))
    focused->grab_focus();
}

void MainWindow::hold_note_scroll()
{
  auto adj = paste_.get_vadjustment();
  if (!adj)
    return;
  if (Gtk::TextView* view = note_view_for(get_focus()))
    note_scroll_view_ = view;
  if (!note_scroll_lock_) {
    note_scroll_held_ = adj->get_value();
    note_scroll_lock_ = true;
    note_scroll_conn_.disconnect();
    note_scroll_conn_ = adj->signal_value_changed().connect([this, adj] {
      if (!note_scroll_lock_ || note_scroll_snapping_)
        return;
      const double page = adj->get_page_size();
      const double room = std::max(adj->get_lower(), adj->get_upper() - std::max(0.0, page));
      const double keep = std::max(adj->get_lower(), std::min(note_scroll_held_, room));
      if (adj->get_value() == keep)
        return;
      note_scroll_snapping_ = true;
      adj->set_value(keep);
      note_scroll_snapping_ = false;
    });
  }
  // Two idle turns: the page-status idle lays the seams out and queues a
  // resize, and that resize runs on the next turn. Releasing on the first
  // would let the clamp run after the lock.
  note_scroll_wait_ = 2;
  if (note_scroll_idle_.connected())
    return;
  note_scroll_idle_ = Glib::signal_idle().connect(
      [this] {
        if (note_scroll_wait_ > 1) {
          --note_scroll_wait_;
          return true;
        }
        note_scroll_idle_.disconnect();
        Gtk::TextView* const view = note_scroll_view_;
        const bool reveal = note_scroll_lock_ && view && get_focus() == view;
        note_scroll_lock_ = false;
        note_scroll_conn_.disconnect();
        note_scroll_wait_ = 0;
        if (reveal)
          reveal_story_caret(*view);
        return false;
      },
      Glib::PRIORITY_LOW);
}

void MainWindow::release_note_scroll(bool reveal)
{
  const bool was = note_scroll_lock_;
  note_scroll_lock_ = false;
  note_scroll_wait_ = 0;
  note_scroll_conn_.disconnect();
  note_scroll_idle_.disconnect();
  Gtk::TextView* const view = note_scroll_view_;
  if (reveal && was && view && get_focus() == view)
    reveal_story_caret(*view);
}

void MainWindow::on_notes_end()
{
  on_note_view_end(notes_view_);
}

void MainWindow::on_note_view_end(Gtk::TextView& view)
{
  if (loading_ || restoring_ || !buffer_ || !view.get_buffer())
    return;
  const bool whole = note_groups_.size() <= 1;
  NoteGroup group;
  if (!whole) {
    if (&view == &notes_view_) {
      group = note_groups_.front();
    } else {
      for (size_t i = 1; i < note_groups_.size() && i - 1 < note_bands_.size(); ++i) {
        if (note_bands_[i - 1] && &note_bands_[i - 1]->view == &view)
          group = note_groups_[i];
      }
      if (group.indexes.empty())
        return;
    }
  }
  const std::string text = view.get_buffer()->get_text().raw();
  const std::string current = whole ? notes_text(notes_) : note_group_text(group);
  if (text == current)
    return;
  note_editing_ = true;
  hold_note_scroll();
  Document before = capture();
  Document after = before;
  std::vector<std::vector<Paragraph>> parsed = notes_from_text(text, typing_);
  if (blank_text(text)) {
    const bool was = loading_;
    loading_ = true;
    view.get_buffer()->set_text("");
    if (&view == &notes_view_)
      notes_shown_ = "";
    loading_ = was;
    parsed.clear();
  }
  if (whole) {
    after.notes = std::move(parsed);
  } else {
    const int first = group.indexes.front();
    const int last = group.indexes.back();
    const bool range = last >= first &&
                       last - first + 1 == static_cast<int>(group.indexes.size()) && first >= 0 &&
                       last < static_cast<int>(after.notes.size());
    if (!range)
      return;
    const int old_count = last - first + 1;
    const int new_count = static_cast<int>(parsed.size());
    after.notes.erase(after.notes.begin() + first, after.notes.begin() + last + 1);
    after.notes.insert(after.notes.begin() + first, std::make_move_iterator(parsed.begin()),
                       std::make_move_iterator(parsed.end()));
    if (static_cast<int>(after.notes.size()) > kMaxNotes)
      after.notes.resize(static_cast<size_t>(kMaxNotes));
    const int delta = new_count - old_count;
    for (NoteGroup& item : note_groups_) {
      if (item.indexes.empty())
        continue;
      if (item.indexes.front() == first) {
        item.indexes.clear();
        for (int k = 0; k < new_count; ++k)
          item.indexes.push_back(first + k);
      } else if (item.indexes.front() > first) {
        for (int& index : item.indexes)
          index += delta;
      }
    }
    note_groups_.erase(std::remove_if(note_groups_.begin(), note_groups_.end(),
                                      [](const NoteGroup& item) { return item.indexes.empty(); }),
                       note_groups_.end());
    if (note_groups_.size() <= 1)
      note_groups_.clear();
  }
  Gtk::Widget* focused = get_focus();
  commit_document(before, after);
  if (focused == &view)
    view.grab_focus();
}

void MainWindow::on_page_setup()
{
  const Units units = settings_.units;
  const PageSetup page = page_setup_;
  Gtk::Dialog dialog("Page Setup", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);

  auto* paper = Gtk::manage(new Gtk::ComboBoxText());
  for (const PaperPreset& preset : kPapers)
    paper->append(preset.name);
  auto* portrait = Gtk::manage(new Gtk::RadioButton("_Portrait", true));
  auto group = portrait->get_group();
  auto* landscape = Gtk::manage(new Gtk::RadioButton(group, "_Landscape", true));
  if (page.landscape)
    landscape->set_active(true);
  const double paper_max = max_measure(units);
  auto* width = measure_spin(units, page.paper_width, paper_max);
  auto* height = measure_spin(units, page.paper_height, paper_max);
  auto* left = measure_spin(units, page.margin_left, paper_max);
  auto* right = measure_spin(units, page.margin_right, paper_max);
  auto* top = measure_spin(units, page.margin_top, paper_max);
  auto* bottom = measure_spin(units, page.margin_bottom, paper_max);

  int base_w = page.paper_width;
  int base_h = page.paper_height;
  int base_l = page.margin_left;
  int base_r = page.margin_right;
  int base_t = page.margin_top;
  int base_b = page.margin_bottom;
  double shown_w = width->get_value();
  double shown_h = height->get_value();
  const double shown_l = left->get_value();
  const double shown_r = right->get_value();
  const double shown_t = top->get_value();
  const double shown_b = bottom->get_value();
  bool applying = false;

  auto swap_paper = [&] {
    applying = true;
    const double w = width->get_value();
    const double h = height->get_value();
    width->set_value(h);
    height->set_value(w);
    std::swap(base_w, base_h);
    std::swap(shown_w, shown_h);
    applying = false;
  };
  landscape->signal_toggled().connect([&] {
    if (!applying)
      swap_paper();
  });
  width->signal_value_changed().connect([&] {
    if (!applying)
      paper->set_active(3);
  });
  height->signal_value_changed().connect([&] {
    if (!applying)
      paper->set_active(3);
  });
  paper->signal_changed().connect([&] {
    if (applying)
      return;
    const int row = paper->get_active_row_number();
    if (row < 0 || row >= 3)
      return;
    const bool land = landscape->get_active();
    const int w = land ? kPapers[row].height : kPapers[row].width;
    const int h = land ? kPapers[row].width : kPapers[row].height;
    applying = true;
    width->set_value(units_round(twips_to_units(w, units), units));
    height->set_value(units_round(twips_to_units(h, units), units));
    base_w = w;
    base_h = h;
    shown_w = width->get_value();
    shown_h = height->get_value();
    applying = false;
  });

  const int portrait_w = page.landscape ? page.paper_height : page.paper_width;
  const int portrait_h = page.landscape ? page.paper_width : page.paper_height;
  int match = 3;
  for (int i = 0; i < 3; ++i) {
    if (portrait_w == kPapers[i].width && portrait_h == kPapers[i].height)
      match = i;
  }
  paper->set_active(match);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  grid->attach(*field_label("_Paper:", *paper), 0, 0, 1, 1);
  grid->attach(*paper, 1, 0, 1, 1);
  grid->attach(*portrait, 2, 0, 1, 1);
  grid->attach(*landscape, 3, 0, 1, 1);
  grid->attach(*field_label("_Width:", *width), 0, 1, 1, 1);
  grid->attach(*width, 1, 1, 1, 1);
  grid->attach(*field_label("_Height:", *height), 2, 1, 1, 1);
  grid->attach(*height, 3, 1, 1, 1);
  auto* margins = Gtk::manage(new Gtk::Frame("Margins"));
  auto* margin_grid = Gtk::manage(new Gtk::Grid());
  margin_grid->set_row_spacing(8);
  margin_grid->set_column_spacing(12);
  margin_grid->set_margin_top(8);
  margin_grid->set_margin_bottom(12);
  margin_grid->set_margin_start(12);
  margin_grid->set_margin_end(12);
  margin_grid->attach(*field_label("_Left:", *left), 0, 0, 1, 1);
  margin_grid->attach(*left, 1, 0, 1, 1);
  margin_grid->attach(*field_label("_Right:", *right), 2, 0, 1, 1);
  margin_grid->attach(*right, 3, 0, 1, 1);
  margin_grid->attach(*field_label("_Top:", *top), 0, 1, 1, 1);
  margin_grid->attach(*top, 1, 1, 1, 1);
  margin_grid->attach(*field_label("_Bottom:", *bottom), 2, 1, 1, 1);
  margin_grid->attach(*bottom, 3, 1, 1, 1);
  margins->add(*margin_grid);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*margins, Gtk::PACK_SHRINK);
  dialog.show_all_children();

  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      text_.grab_focus();
      return;
    }
    PageSetup next = page;
    std::string message;
    Gtk::SpinButton* bad = width;
    auto take = [&](Gtk::SpinButton& button, int original, double shown, int& dest) {
      if (!message.empty())
        return;
      if (!read_measure(button, units, original, shown, dest, message))
        bad = &button;
    };
    take(*width, base_w, shown_w, next.paper_width);
    take(*height, base_h, shown_h, next.paper_height);
    take(*left, base_l, shown_l, next.margin_left);
    take(*right, base_r, shown_r, next.margin_right);
    take(*top, base_t, shown_t, next.margin_top);
    take(*bottom, base_b, shown_b, next.margin_bottom);
    if (!message.empty()) {
      warn(dialog, message, *bad);
      continue;
    }
    next.landscape = landscape->get_active();
    if (next.paper_width < kMinPaperTwips || next.paper_width > kMaxPaperTwips) {
      warn(dialog, paper_range(units), *width);
      continue;
    }
    if (next.paper_height < kMinPaperTwips || next.paper_height > kMaxPaperTwips) {
      warn(dialog, paper_range(units), *height);
      continue;
    }
    if (next.paper_width - next.margin_left - next.margin_right < kMinTextTwips) {
      warn(dialog, text_room(units), *left);
      continue;
    }
    if (next.paper_height - next.margin_top - next.margin_bottom < kMinTextTwips) {
      warn(dialog, text_room(units), *top);
      continue;
    }
    if (next == page) {
      text_.grab_focus();
      return;
    }
    Document before = capture();
    Document after = before;
    after.page = next;
    commit_document(before, after);
    return;
  }
}

void MainWindow::on_columns()
{
  const Units units = settings_.units;
  const PageSetup page = page_setup_;
  Gtk::Dialog dialog("Columns", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);

  const int upper = std::max(4, page.columns);
  auto* columns =
      Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(page.columns, 1, upper, 1, 1), 1, 0));
  columns->set_numeric(true);
  columns->set_activates_default(true);
  const double gap_max = twips_to_units(kMaxColumnGap, units);
  auto* gap = measure_spin(units, page.column_gap, gap_max);
  const int base_gap = page.column_gap;
  const double shown_gap = gap->get_value();
  gap->set_sensitive(page.columns != 1);
  columns->signal_value_changed().connect(
      [columns, gap] { gap->set_sensitive(columns->get_value_as_int() != 1); });

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  grid->attach(*field_label("_Number of columns:", *columns), 0, 0, 1, 1);
  grid->attach(*columns, 1, 0, 1, 1);
  grid->attach(*field_label("_Spacing:", *gap), 0, 1, 1, 1);
  grid->attach(*gap, 1, 1, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all_children();

  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      text_.grab_focus();
      return;
    }
    PageSetup next = page;
    next.columns = std::max(1, std::min(upper, columns->get_value_as_int()));
    if (next.columns == 1) {
      next.column_gap = default_page().column_gap;
    } else {
      std::string message;
      if (!read_measure(*gap, units, base_gap, shown_gap, next.column_gap, message)) {
        warn(dialog, message, *gap);
        continue;
      }
      if (next.column_gap < 0 || next.column_gap > kMaxColumnGap) {
        warn(dialog,
             "The measurement must be between " + format_measure(0, units) + " and " +
                 format_measure(twips_to_units(kMaxColumnGap, units), units) + ".",
             *gap);
        continue;
      }
    }
    if (next == page) {
      text_.grab_focus();
      return;
    }
    Document before = capture();
    Document after = before;
    after.page = next;
    commit_document(before, after);
    return;
  }
}

bool MainWindow::caret_in_table() const
{
  if (!buffer_)
    return false;
  // The Insert menu takes the keyboard. The cell that was edited stays
  // recorded, and the body caret stays on the table's anchor.
  if (editing_cell() || sheet_at_cursor())
    return true;
  return para_at(cursor_offset()).cell.table != 0;
}

void MainWindow::on_page_break()
{
  if (!buffer_)
    return;
  // A page break has no meaning inside a table. The command leaves the
  // document and the caret where they are, and the status line says why.
  if (caret_in_table()) {
    message_.set_text("Cannot break the page inside a table.");
    // The same cell, not the top-left one. focus_table_caret() would jump.
    restore_edit_focus();
    return;
  }
  if (message_.get_text() == "Cannot break the page inside a table.")
    message_.set_text("");
  Document before = capture();
  if (before.paragraphs.empty())
    before.paragraphs.emplace_back();
  Document after = before;
  size_t index = caret_paragraph();
  if (index >= after.paragraphs.size())
    index = after.paragraphs.size() - 1;
  const int origin = chars_before(after.paragraphs, index);
  int into = cursor_offset() - origin;
  const int length = chars_of(after.paragraphs[index]);
  if (into < 0)
    into = 0;
  if (into > length)
    into = length;
  int caret = origin;
  if (into == 0) {
    if (after.paragraphs[index].page_break) {
      text_.grab_focus();
      return;
    }
    after.paragraphs[index].page_break = true;
  } else if (into == length && index + 1 == after.paragraphs.size()) {
    Paragraph next = after.paragraphs[index];
    next.runs.clear();
    next.mark.reset();
    next.page_break = false;
    next.heading = 0;
    after.paragraphs.push_back(next);
    const size_t added = after.paragraphs.size() - 1;
    const std::string style = next_style(style_sheet(after), after.paragraphs[index].style);
    apply_style(after, added, added, style);
    after.paragraphs[added].page_break = true;
    caret = chars_before(after.paragraphs, added);
  } else if (into == length) {
    if (after.paragraphs[index + 1].page_break) {
      text_.grab_focus();
      return;
    }
    after.paragraphs[index + 1].page_break = true;
    caret = chars_before(after.paragraphs, index + 1);
  } else {
    Paragraph right;
    split_at(after.paragraphs[index], into, right);
    after.paragraphs.insert(after.paragraphs.begin() + static_cast<std::ptrdiff_t>(index + 1),
                            right);
    caret = chars_before(after.paragraphs, index + 1);
  }
  commit_document(before, after, caret);
}

void MainWindow::on_insert_table()
{
  if (!buffer_)
    return;
  Gtk::Dialog dialog("Insert Table", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  auto* columns =
      Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(2, 1, kMaxTableColumns, 1, 1), 1, 0));
  auto* rows =
      Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(2, 1, kMaxTableRows, 1, 1), 1, 0));
  columns->set_numeric(true);
  rows->set_numeric(true);
  columns->set_activates_default(true);
  rows->set_activates_default(true);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  grid->attach(*field_label("_Number of columns:", *columns), 0, 0, 1, 1);
  grid->attach(*columns, 1, 0, 1, 1);
  grid->attach(*field_label("Number of _rows:", *rows), 0, 1, 1, 1);
  grid->attach(*rows, 1, 1, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all_children();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    text_.grab_focus();
    return;
  }
  Document before = capture();
  Document after = before;
  size_t at = caret_paragraph();
  if (at > after.paragraphs.size())
    at = after.paragraphs.size();
  if (in_table(after.paragraphs, at)) {
    const int id = after.paragraphs[at].cell.table;
    while (at < after.paragraphs.size() && after.paragraphs[at].cell.table == id)
      ++at;
  }
  if (!insert_table(after.paragraphs, at, rows->get_value_as_int(), columns->get_value_as_int(),
                    page_text_twips(page_setup_))) {
    text_.grab_focus();
    return;
  }
  commit_document(before, after, chars_before(after.paragraphs, at));
}

void MainWindow::change_table_at_caret(bool (*edit)(std::vector<Paragraph>&, size_t))
{
  if (!buffer_ || edit == nullptr)
    return;
  CellEdit saved = cell_edit_;
  bool restore_cell = false;
  size_t index = static_cast<size_t>(-1);
  if (TableSheet* sheet = editing_sheet()) {
    int row = 0;
    int column = 0;
    int caret = 0;
    int bound = 0;
    if (cell_place(*sheet, row, column, caret, bound)) {
      const int line = sheet->line_at(row, column, std::min(caret, bound));
      index = cell_paragraph_index(*sheet, row, column, line);
      saved.on = true;
      saved.table = sheet->table_id();
      saved.row = row;
      saved.column = column;
      saved.caret = caret;
      saved.bound = bound;
      restore_cell = index != static_cast<size_t>(-1);
    }
  }
  if (index == static_cast<size_t>(-1))
    index = caret_paragraph();
  Document before = capture();
  Document after = before;
  if (index >= after.paragraphs.size() || !edit(after.paragraphs, index) ||
      after.paragraphs == before.paragraphs) {
    if (restore_cell)
      restore_edit_focus();
    else
      text_.grab_focus();
    return;
  }
  // A click in a cell leaves the body insert where it was, often the first
  // line. The rebuild follows that insert, so the caret and the pasteboard
  // jump to the start of the document. Park the insert on this table, and
  // keep the pasteboard where the table already is.
  const int count = buffer_->get_char_count();
  const int anchor = std::max(0, std::min(chars_before(before.paragraphs, index), count));
  hold_view();
  buffer_->place_cursor(buffer_->get_iter_at_offset(anchor));
  commit_document(before, after, anchor);
  if (restore_cell)
    restore_cell_edit(saved);
  release_view();
}

void MainWindow::on_insert_row()
{
  change_table_at_caret(insert_table_row);
}

void MainWindow::on_insert_column()
{
  change_table_at_caret(insert_table_column);
}

void MainWindow::on_delete_row()
{
  change_table_at_caret(delete_table_row);
}

void MainWindow::on_delete_column()
{
  change_table_at_caret(delete_table_column);
}

void MainWindow::commit_table_widths(int table_id, const std::vector<int>& widths)
{
  if (!buffer_ || table_id == 0 || widths.empty())
    return;
  const bool restore_cell = editing_cell();
  const CellEdit saved = cell_edit_;
  const Document before = capture();
  size_t index = before.paragraphs.size();
  for (size_t i = 0; i < before.paragraphs.size(); ++i) {
    if (before.paragraphs[i].cell.table == table_id) {
      index = i;
      break;
    }
  }
  if (!in_table(before.paragraphs, index) || before.paragraphs[index].cell.widths == widths)
    return;
  Document after = before;
  if (!apply_table_widths(after.paragraphs, index, widths) || after.paragraphs == before.paragraphs)
    return;
  hold_view();
  commit_document(before, after);
  if (restore_cell)
    restore_cell_edit(saved);
  release_view();
}

void MainWindow::on_table_width()
{
  if (!buffer_)
    return;
  const bool restore_cell = editing_cell();
  CellEdit saved = cell_edit_;
  Document before = capture();
  size_t index = static_cast<size_t>(-1);
  int column = 0;
  if (restore_cell) {
    if (TableSheet* sheet = editing_sheet()) {
      int row = 0;
      int caret = 0;
      int bound = 0;
      if (cell_place(*sheet, row, column, caret, bound)) {
        const int at = std::min(caret, bound);
        const int line = sheet->line_at(row, column, at);
        index = cell_paragraph_index(*sheet, row, column, line);
        saved.on = true;
        saved.table = sheet->table_id();
        saved.row = row;
        saved.column = column;
        saved.caret = caret;
        saved.bound = bound;
      }
    }
  }
  if (index >= before.paragraphs.size() || !in_table(before.paragraphs, index)) {
    if (TableSheet* at_sheet = sheet_at_cursor()) {
      const int id = at_sheet->table_id();
      for (size_t i = 0; i < before.paragraphs.size(); ++i) {
        if (before.paragraphs[i].cell.table == id) {
          index = i;
          break;
        }
      }
    }
  }
  if (index >= before.paragraphs.size() || !in_table(before.paragraphs, index)) {
    index = caret_paragraph();
    if (!in_table(before.paragraphs, index)) {
      if (restore_cell)
        restore_edit_focus();
      else
        text_.grab_focus();
      return;
    }
    column = before.paragraphs[index].cell.column;
  }
  const Cell cell = before.paragraphs[index].cell;
  if (cell.columns < 1) {
    if (restore_cell)
      restore_edit_focus();
    else
      text_.grab_focus();
    return;
  }
  column = cell.column;
  std::vector<int> working = cell.widths;
  if (static_cast<int>(working.size()) != cell.columns)
    working.assign(static_cast<size_t>(cell.columns), kMinCellTwips);
  if (column < 0 || column >= static_cast<int>(working.size()))
    column = 0;

  long long wide = 0;
  for (int width : working) {
    wide += std::max(0, width);
    if (wide > 2000000000LL)
      wide = 2000000000LL;
  }
  const int sum = static_cast<int>(wide);
  const int floor_sum = kMinCellTwips * static_cast<int>(working.size());
  // A table may reach the page edge, through the right margin. One that is
  // already wider keeps that width and cannot grow. The floor is the
  // narrowest the columns can be.
  const int limit = std::max(floor_sum, std::max(table_width_limit(page_setup_), sum));
  const int column_cap =
      std::max(working[static_cast<size_t>(column)], limit - kMinCellTwips * (cell.columns - 1));

  const Units units = settings_.units;
  Gtk::Dialog dialog("Table Width", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);

  const double column_upper =
      std::max(units_round(twips_to_units(column_cap, units), units),
               units_round(twips_to_units(working[static_cast<size_t>(column)], units), units));
  const double table_upper = std::max(units_round(twips_to_units(limit, units), units),
                                      units_round(twips_to_units(sum, units), units));
  auto* column_spin = measure_spin(units, working[static_cast<size_t>(column)], column_upper);
  auto* table_spin = measure_spin(units, sum, table_upper);

  const std::string column_text = "_Column " + std::to_string(column + 1) + ":";
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  grid->attach(*field_label(column_text.c_str(), *column_spin), 0, 0, 1, 1);
  grid->attach(*column_spin, 1, 0, 1, 1);
  grid->attach(*field_label("Table _width:", *table_spin), 0, 1, 1, 1);
  grid->attach(*table_spin, 1, 1, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all_children();

  const std::vector<int> initial = working;
  std::vector<int> preview = working;
  bool guard = false;
  auto total_of = [](const std::vector<int>& widths) {
    int total = 0;
    for (int width : widths)
      total += width;
    return total;
  };
  auto assign_spin = [&](Gtk::SpinButton& spin, int twips) {
    const double shown = units_round(twips_to_units(twips, units), units);
    const double delta = spin.get_value() - shown;
    if (delta < 0.001 && delta > -0.001)
      return;
    spin.set_value(shown);
  };
  auto show = [&] {
    guard = true;
    assign_spin(*column_spin, preview[static_cast<size_t>(column)]);
    assign_spin(*table_spin, total_of(preview));
    guard = false;
  };
  column_spin->signal_value_changed().connect([&] {
    if (guard)
      return;
    std::vector<int> next = preview;
    if (!set_table_column_width(next, column, units_to_twips(column_spin->get_value(), units),
                                limit))
      return;
    preview = std::move(next);
    show();
  });
  table_spin->signal_value_changed().connect([&] {
    if (guard)
      return;
    std::vector<int> next = preview;
    if (!fit_table_widths(next, units_to_twips(table_spin->get_value(), units), limit))
      return;
    preview = std::move(next);
    show();
  });

  auto close_focus = [&] {
    if (restore_cell)
      restore_edit_focus();
    else
      text_.grab_focus();
  };
  const std::string too_narrow =
      "The table must be at least " + format_measure(twips_to_units(floor_sum, units), units) + ".";

  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      close_focus();
      return;
    }
    // A typed number that has not been emitted yet. The focused field is
    // last, so a column typed on top of a fitted table is the one that stays.
    Gtk::Widget* focus = dialog.get_focus();
    if (focus != column_spin)
      column_spin->update();
    if (focus != table_spin)
      table_spin->update();
    if (focus == column_spin)
      column_spin->update();
    else if (focus == table_spin)
      table_spin->update();

    std::string message;
    int column_twips = 0;
    int table_twips = 0;
    if (!read_measure(*column_spin, units, preview[static_cast<size_t>(column)],
                      column_spin->get_value(), column_twips, message)) {
      warn(dialog, message, *column_spin);
      continue;
    }
    if (!read_measure(*table_spin, units, total_of(preview), table_spin->get_value(), table_twips,
                      message)) {
      warn(dialog, message, *table_spin);
      continue;
    }
    if (focus == column_spin && column_twips != preview[static_cast<size_t>(column)]) {
      if (!set_table_column_width(preview, column, column_twips, limit)) {
        warn(dialog, too_narrow, *column_spin);
        continue;
      }
    } else if (focus == table_spin && table_twips != total_of(preview)) {
      if (!fit_table_widths(preview, table_twips, limit)) {
        warn(dialog, too_narrow, *table_spin);
        continue;
      }
    }
    if (preview == initial) {
      close_focus();
      return;
    }
    Document after = before;
    if (!apply_table_widths(after.paragraphs, index, preview) ||
        after.paragraphs == before.paragraphs) {
      close_focus();
      return;
    }
    if (restore_cell && cell_edit_.on)
      saved = cell_edit_;
    hold_view();
    commit_document(before, after);
    if (restore_cell)
      restore_cell_edit(saved);
    release_view();
    return;
  }
}

void MainWindow::on_footnote()
{
  if (!buffer_ || notes_.size() >= static_cast<size_t>(kMaxNotes))
    return;
  Document before = capture();
  Document after = before;
  if (after.paragraphs.empty())
    after.paragraphs.emplace_back();
  const int number = static_cast<int>(after.notes.size()) + 1;
  after.notes.push_back(std::vector<Paragraph>{Paragraph{}});

  bool in_cell = false;
  int body_caret = -1;
  if (TableSheet* sheet = editing_sheet()) {
    int row = 0;
    int column = 0;
    int caret = 0;
    int bound = 0;
    Gtk::TextView* view = nullptr;
    if (cell_place(*sheet, row, column, caret, bound))
      view = sheet->view_at(row, column);
    auto cell_buffer = view ? view->get_buffer() : Glib::RefPtr<Gtk::TextBuffer>{};
    const int at = std::min(caret, bound);
    const int line = sheet->line_at(row, column, at);
    const size_t index =
        cell_buffer ? cell_paragraph_index(*sheet, row, column, line) : static_cast<size_t>(-1);
    if (cell_buffer && index < after.paragraphs.size() &&
        after.paragraphs[index].cell.table == sheet->table_id() &&
        after.paragraphs[index].cell.row == row && after.paragraphs[index].cell.column == column) {
      int line_start = 0;
      int seen = 0;
      for (auto iter = cell_buffer->begin(); seen < line && !iter.is_end(); ++iter) {
        if (iter.get_char() != '\n')
          continue;
        ++seen;
        auto next = iter;
        next.forward_char();
        line_start = next.get_offset();
      }
      int into = at - line_start;
      if (into < 0)
        into = 0;
      const int length = chars_of(after.paragraphs[index]);
      if (into > length)
        into = length;
      Run basis = typing_;
      const int count = cell_buffer->get_char_count();
      const int probe = std::max(0, std::min(at, count));
      if (probe > 0) {
        auto prev = cell_buffer->get_iter_at_offset(probe - 1);
        if (prev.get_char() != '\n')
          basis = format_of(prev);
      } else if (probe < count) {
        auto here = cell_buffer->get_iter_at_offset(probe);
        if (here.get_char() != '\n')
          basis = format_of(here);
      }
      Run marker = plain_run(basis, std::to_string(number));
      marker.note = number;
      insert_run(after.paragraphs[index], into, marker);
      in_cell = true;
    }
  }
  if (!in_cell) {
    size_t index = caret_paragraph();
    if (index >= after.paragraphs.size())
      index = after.paragraphs.size() - 1;
    const int origin = chars_before(after.paragraphs, index);
    int into = cursor_offset() - origin;
    const int length = chars_of(after.paragraphs[index]);
    if (into < 0)
      into = 0;
    if (into > length)
      into = length;
    Run marker = plain_run(typing_, std::to_string(number));
    marker.note = number;
    insert_run(after.paragraphs[index], into, marker);
    body_caret = origin + into + static_cast<int>(Glib::ustring(marker.text).length());
  }

  // Before the rebuild. The caret idle would otherwise follow the digit.
  reveal_note_ = number;
  if (in_cell)
    commit_document(before, after);
  else
    commit_document(before, after, body_caret);
  // The new marker's sheet is known once the seams are laid out. Until then
  // every note still shares the first column.
  update_page_seams();
  place_notes();
  Gtk::TextView* view = view_for_note(number);
  if (!view)
    view = &notes_view_;
  if (auto notes = view->get_buffer())
    notes->place_cursor(notes->end());
  view->grab_focus();
}

void MainWindow::on_picture()
{
  if (!buffer_)
    return;
  Gtk::FileChooserDialog dialog(*this, "Insert Picture", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Insert", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);
  auto filter = Gtk::FileFilter::create();
  filter->set_name("Pictures");
  filter->add_mime_type("image/png");
  filter->add_mime_type("image/jpeg");
  filter->add_pattern("*.png");
  filter->add_pattern("*.jpg");
  filter->add_pattern("*.jpeg");
  filter->add_pattern("*.PNG");
  filter->add_pattern("*.JPG");
  filter->add_pattern("*.JPEG");
  dialog.add_filter(filter);
  if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
    text_.grab_focus();
    return;
  }
  Image image;
  image.path = dialog.get_filename();
  image.alt = Glib::path_get_basename(image.path);
  if (!load_image_file(image.path, image)) {
    tell("Write-It can insert a PNG or JPEG picture.");
    text_.grab_focus();
    return;
  }
  insert_picture(std::move(image));
}

void MainWindow::insert_picture(Image image)
{
  if (!buffer_)
    return;
  if (auto pix = pixbuf_from_bytes(image.data)) {
    image.width = goal_twips(pix->get_width());
    image.height = goal_twips(pix->get_height());
  }
  Document before = capture();
  Document after = before;
  if (after.paragraphs.empty())
    after.paragraphs.emplace_back();

  // The same cell the footnote marker uses. Opening Insert, then the file
  // dialog, leaves the cell; cell_edit_ is still that cell.
  if (TableSheet* sheet = editing_sheet()) {
    int row = 0;
    int column = 0;
    int caret = 0;
    int bound = 0;
    Gtk::TextView* view = nullptr;
    if (cell_place(*sheet, row, column, caret, bound))
      view = sheet->view_at(row, column);
    auto cell_buffer = view ? view->get_buffer() : Glib::RefPtr<Gtk::TextBuffer>{};
    const int at = std::min(caret, bound);
    const int line = sheet->line_at(row, column, at);
    const size_t index =
        cell_buffer ? cell_paragraph_index(*sheet, row, column, line) : static_cast<size_t>(-1);
    if (cell_buffer && index < after.paragraphs.size() &&
        after.paragraphs[index].cell.table == sheet->table_id() &&
        after.paragraphs[index].cell.row == row && after.paragraphs[index].cell.column == column) {
      int line_start = 0;
      int seen = 0;
      for (auto iter = cell_buffer->begin(); seen < line && !iter.is_end(); ++iter) {
        if (iter.get_char() != '\n')
          continue;
        ++seen;
        auto next = iter;
        next.forward_char();
        line_start = next.get_offset();
      }
      int into = at - line_start;
      if (into < 0)
        into = 0;
      const int length = chars_of(after.paragraphs[index]);
      if (into > length)
        into = length;
      Run run;
      run.image = std::move(image);
      insert_run(after.paragraphs[index], into, run);
      // A picture is one character. The keyboard returns just after it.
      CellEdit saved;
      saved.on = true;
      saved.table = sheet->table_id();
      saved.row = row;
      saved.column = column;
      saved.caret = into + 1;
      saved.bound = into + 1;
      // The body insert is often still the first line. Park it on this table
      // so the rebuild does not jump the pasteboard there.
      const int count = buffer_->get_char_count();
      const int anchor = std::max(0, std::min(chars_before(before.paragraphs, index), count));
      hold_view();
      buffer_->place_cursor(buffer_->get_iter_at_offset(anchor));
      commit_document(before, after, anchor);
      restore_cell_edit(saved);
      release_view();
      return;
    }
  }

  size_t index = caret_paragraph();
  if (index >= after.paragraphs.size())
    index = after.paragraphs.size() - 1;
  const int origin = chars_before(after.paragraphs, index);
  int into = cursor_offset() - origin;
  const int length = chars_of(after.paragraphs[index]);
  if (into < 0)
    into = 0;
  if (into > length)
    into = length;
  Run run;
  run.image = std::move(image);
  insert_run(after.paragraphs[index], into, run);
  commit_document(before, after, origin + into + 1);
}

Glib::RefPtr<Gdk::Pixbuf> MainWindow::pixbuf_for(const Image& image)
{
  auto scaled = [](const Glib::RefPtr<Gdk::Pixbuf>& pix) {
    if (!pix || pix->get_width() <= kPicturePx || pix->get_width() <= 0)
      return pix;
    const int height = std::max(1, pix->get_height() * kPicturePx / pix->get_width());
    return pix->scale_simple(kPicturePx, height, Gdk::INTERP_BILINEAR);
  };
  if (image.data.empty())
    return placeholder_pixbuf();
  if (auto pix = pixbuf_from_bytes(image.data))
    return scaled(pix);
  return placeholder_pixbuf();
}

}  // namespace writeit
