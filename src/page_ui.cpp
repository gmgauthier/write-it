/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "para_check.hpp"

#include <giomm/memoryinputstream.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace writeit {
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

TableSheet* sheet_from(const Glib::RefPtr<Gtk::TextChildAnchor>& anchor)
{
  if (!anchor)
    return nullptr;
  return static_cast<TableSheet*>(g_object_get_data(G_OBJECT(anchor->gobj()), "writeit-table"));
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
        view->get_buffer()->signal_begin_user_action().connect([this] { window_.on_cell_begin(); });
        view->get_buffer()->signal_end_user_action().connect([this] { window_.on_cell_end(); });
        view->signal_focus_in_event().connect([this](GdkEventFocus*) {
          window_.update_actions();
          return false;
        });
        // The cell places the caret, then this click bubbles to the body
        // text view, whose gesture takes the focus back. Keep the click.
        view->signal_button_press_event().connect([](GdkEventButton*) { return true; }, true);
        view->signal_button_release_event().connect([](GdkEventButton*) { return true; }, true);
        view->signal_motion_notify_event().connect([](GdkEventMotion*) { return true; }, true);
        frame->add_events(Gdk::BUTTON_PRESS_MASK);
        frame->signal_button_press_event().connect([view](GdkEventButton* event) {
          if (!event || event->type != GDK_BUTTON_PRESS)
            return false;
          if (event->button == 1) {
            view->grab_focus();
            view->get_buffer()->place_cursor(view->get_buffer()->begin());
          }
          return true;
        });
      }
    }
    relayout();
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
      }
    }
    queue_resize();
  }

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
    if ((mods & GDK_CONTROL_MASK) == 0)
      return false;
    if (event->keyval == GDK_KEY_z && !shift) {
      MainWindow& window = window_;
      Glib::signal_idle().connect_once([&window] { window.undo(); });
      return true;
    }
    if (event->keyval == GDK_KEY_y || (event->keyval == GDK_KEY_z && shift) ||
        event->keyval == GDK_KEY_Z) {
      MainWindow& window = window_;
      Glib::signal_idle().connect_once([&window] { window.redo(); });
      return true;
    }
    if (event->keyval == GDK_KEY_a || event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_v ||
        event->keyval == GDK_KEY_x || event->keyval == GDK_KEY_A || event->keyval == GDK_KEY_C ||
        event->keyval == GDK_KEY_V || event->keyval == GDK_KEY_X)
      return false;
    return true;
  }

  MainWindow& window_;
  int rows_ = 0;
  int cols_ = 0;
  std::vector<int> widths_;
  std::vector<std::vector<Gtk::TextView*>> views_;
  std::vector<std::vector<CellFrame*>> frames_;
  std::vector<std::vector<std::vector<Paragraph>>> forms_;
};

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
    if (text == shown && view.get_buffer()->get_text().raw() == text)
      return;
    const bool was = loading_;
    loading_ = true;
    view.get_buffer()->set_text(text);
    shown = text;
    loading_ = was;
  };
  fill(header_view_, story_text(header_), header_shown_);
  fill(footer_view_, story_text(footer_), footer_shown_);
  fill(notes_view_, notes_text(notes_), notes_shown_);

  const bool page = view_ == ViewMode::Page;
  const bool show_frames = page && stories_on_;
  const bool show_notes = page && !notes_.empty();
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
  attach(notes_view_, show_notes);
  attach(footer_view_, show_frames);
  int pos = 0;
  auto order = [this, &pos](Gtk::Widget& widget) {
    if (widget.get_parent() == &page_box_)
      page_box_.reorder_child(widget, pos++);
  };
  order(header_view_);
  order(text_);
  order(notes_view_);
  order(footer_view_);
  if (!loading_)
    apply_page_size();
}

bool MainWindow::on_story_key(GdkEventKey* event)
{
  if (!event)
    return false;
  const guint mods = event->state & gtk_accelerator_get_default_mod_mask();
  if ((mods & GDK_CONTROL_MASK) == 0)
    return false;
  const bool shift = (mods & GDK_SHIFT_MASK) != 0;
  if (event->keyval == GDK_KEY_z && !shift) {
    undo();
    return true;
  }
  if (event->keyval == GDK_KEY_y || (event->keyval == GDK_KEY_z && shift) ||
      event->keyval == GDK_KEY_Z) {
    redo();
    return true;
  }
  if (event->keyval == GDK_KEY_a || event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_v ||
      event->keyval == GDK_KEY_x || event->keyval == GDK_KEY_A || event->keyval == GDK_KEY_C ||
      event->keyval == GDK_KEY_V || event->keyval == GDK_KEY_X)
    return false;
  return true;
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
  if (focused == &header_view_ || focused == &footer_view_ || focused == &notes_view_)
    focused->grab_focus();
}

void MainWindow::on_notes_end()
{
  if (loading_ || restoring_ || !buffer_)
    return;
  const std::string text = notes_view_.get_buffer()->get_text().raw();
  if (text == notes_text(notes_))
    return;
  Document before = capture();
  Document after = before;
  after.notes = notes_from_text(text, typing_);
  Gtk::Widget* focused = get_focus();
  if (blank_text(text)) {
    const bool was = loading_;
    loading_ = true;
    notes_view_.get_buffer()->set_text("");
    notes_shown_ = "";
    loading_ = was;
  }
  commit_document(before, after);
  if (focused == &notes_view_)
    notes_view_.grab_focus();
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

void MainWindow::on_page_break()
{
  if (!buffer_)
    return;
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

void MainWindow::on_insert_row()
{
  if (!buffer_)
    return;
  Document before = capture();
  Document after = before;
  if (!insert_table_row(after.paragraphs, caret_paragraph())) {
    text_.grab_focus();
    return;
  }
  commit_document(before, after);
}

void MainWindow::on_insert_column()
{
  if (!buffer_)
    return;
  Document before = capture();
  Document after = before;
  if (!insert_table_column(after.paragraphs, caret_paragraph())) {
    text_.grab_focus();
    return;
  }
  commit_document(before, after);
}

void MainWindow::on_delete_row()
{
  if (!buffer_)
    return;
  Document before = capture();
  Document after = before;
  if (!delete_table_row(after.paragraphs, caret_paragraph())) {
    text_.grab_focus();
    return;
  }
  commit_document(before, after);
}

void MainWindow::on_delete_column()
{
  if (!buffer_)
    return;
  Document before = capture();
  Document after = before;
  if (!delete_table_column(after.paragraphs, caret_paragraph())) {
    text_.grab_focus();
    return;
  }
  commit_document(before, after);
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
  const int caret = origin + into + static_cast<int>(Glib::ustring(marker.text).length());
  commit_document(before, after, caret);
  if (notes_view_.get_parent())
    notes_view_.grab_focus();
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
  try {
    auto stream = Gio::MemoryInputStream::create();
    stream->add_data(image.data);
    if (auto pix = Gdk::Pixbuf::create_from_stream(stream)) {
      image.width = goal_twips(pix->get_width());
      image.height = goal_twips(pix->get_height());
    }
  } catch (const Glib::Error&) {
  }
  Document before = capture();
  Document after = before;
  if (after.paragraphs.empty())
    after.paragraphs.emplace_back();
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
  try {
    auto stream = Gio::MemoryInputStream::create();
    stream->add_data(image.data);
    return scaled(Gdk::Pixbuf::create_from_stream(stream));
  } catch (const Glib::Error&) {
    return placeholder_pixbuf();
  }
}

}  // namespace writeit
