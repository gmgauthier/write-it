/* SPDX-License-Identifier: Unlicense */

// Named styles in the editor: the toolbar's style box, Enter's next style,
// and Format > Style....

#include "main_window.hpp"

#include "units.hpp"

#include <algorithm>
#include <cmath>

namespace writeit {
namespace {

// A measure field in the Options unit, as in Format > Paragraph....
Gtk::SpinButton* measure_spin(Units units, int twips, bool signed_value)
{
  const double step = units_step(units);
  const double max_value = std::floor(twips_to_units(kMaxIndent, units) * 100.0 + 1e-9) / 100.0;
  auto* button = Gtk::manage(new Gtk::SpinButton(
      Gtk::Adjustment::create(0, signed_value ? -max_value : 0, max_value, step, step * 5), step,
      static_cast<guint>(units_digits(units))));
  button->set_numeric(false);
  button->set_update_policy(Gtk::UPDATE_IF_VALID);
  button->set_width_chars(8);
  button->signal_output().connect(
      [button, units] {
        button->set_text(format_measure(button->get_value(), units));
        return true;
      },
      false);
  button->signal_input().connect(
      [button, units](double* value) {
        double parsed = 0;
        if (!parse_measure(button->get_text().raw(), units, parsed))
          return GTK_INPUT_ERROR;
        *value = parsed;
        return 1;
      },
      true);
  button->signal_focus_out_event().connect(
      [button, units](GdkEventFocus*) {
        button->update();
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
  auto* label = Gtk::manage(new Gtk::Label(text, true));
  label->set_halign(Gtk::ALIGN_START);
  label->set_mnemonic_widget(target);
  return label;
}

// Whether `name` is `ancestor` or based on it, however far up.
bool descends_from(const std::vector<Style>& sheet, const std::string& name,
                   const std::string& ancestor)
{
  const Style* at = find_style(sheet, name);
  for (size_t steps = 0; at && steps <= sheet.size(); ++steps) {
    if (at->name == ancestor)
      return true;
    at = at->based_on.empty() ? nullptr : find_style(sheet, at->based_on);
  }
  return false;
}

// `current` with each attribute the dialog changed (`edited` differing from
// `shown`), so an edit to a style and to its base both land.
Style merge_edit(Style current, const Style& shown, const Style& edited)
{
  if (edited.based_on != shown.based_on)
    current.based_on = edited.based_on;
  if (edited.next != shown.next)
    current.next = edited.next;
  if (edited.format.font != shown.format.font)
    current.format.font = edited.format.font;
  if (edited.format.size != shown.format.size)
    current.format.size = edited.format.size;
  if (edited.format.bold != shown.format.bold)
    current.format.bold = edited.format.bold;
  if (edited.format.italic != shown.format.italic)
    current.format.italic = edited.format.italic;
  if (edited.format.underline != shown.format.underline)
    current.format.underline = edited.format.underline;
  if (edited.indents.left != shown.indents.left)
    current.indents.left = edited.indents.left;
  if (edited.indents.right != shown.indents.right)
    current.indents.right = edited.indents.right;
  if (edited.indents.first != shown.indents.first)
    current.indents.first = edited.indents.first;
  if (edited.align != shown.align)
    current.align = edited.align;
  if (edited.heading != shown.heading)
    current.heading = edited.heading;
  return current;
}

// A box of style names shows at most this many characters of one, the rest
// ellipsized, so a long name (up to kMaxStyleName bytes, as Word 97 allows)
// widens neither the box, its list, nor the dialog it sits in. The full name
// is the box's tooltip.
constexpr int kStyleNameChars = 32;

void narrow_name_box(Gtk::ComboBox& box)
{
  for (Gtk::CellRenderer* cell : box.get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
      text->property_max_width_chars() = kStyleNameChars;
    }
  }
}

void name_tooltip(Gtk::ComboBoxText& box)
{
  box.signal_changed().connect([&box] {
    const Glib::ustring name = box.get_active_text();
    if (name.length() > static_cast<Glib::ustring::size_type>(kStyleNameChars))
      box.set_tooltip_text(name);
    else
      box.set_has_tooltip(false);
  });
}

}  // namespace

std::vector<Style> MainWindow::sheet() const
{
  Document doc;
  doc.styles = styles_;
  return complete_sheet(style_sheet(doc));
}

int MainWindow::paragraph_index(int offset) const
{
  if (!buffer_)
    return 0;
  const std::string before =
      buffer_->get_text(buffer_->begin(), buffer_->get_iter_at_offset(offset), true).raw();
  return static_cast<int>(std::count(before.begin(), before.end(), '\n'));
}

void MainWindow::fill_style_combo()
{
  const bool guard = suppress_format_;
  suppress_format_ = true;
  narrow_name_box(style_combo_);
  style_combo_.remove_all();
  for (const Style& style : sheet())
    style_combo_.append(style.name);
  suppress_format_ = guard;
  show_style();
}

void MainWindow::show_style()
{
  if (!buffer_)
    return;
  const std::vector<Style> styles = sheet();
  auto style_at = [&](int offset) {
    const Style* style = find_style(styles, para_at(offset).style);
    return style ? style : &styles.front();
  };
  const Style* style = style_at(cursor_offset());
  // Blank over paragraphs in more than one style, as in Word 97.
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  if (buffer_->get_selection_bounds(start_iter, end_iter)) {
    const int start = start_iter.get_offset();
    const int end = end_iter.get_offset();
    const int last = paragraph_start(end > start ? end - 1 : end);
    for (int at = paragraph_start(start); style; at = paragraph_end(at)) {
      if (style_at(at) != style_at(start))
        style = nullptr;
      if (at >= last)
        break;
    }
    if (style)
      style = style_at(start);
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

void MainWindow::on_style_chosen()
{
  if (suppress_format_ || loading_ || restoring_ || !buffer_)
    return;
  const Glib::ustring name = style_combo_.get_active_text();
  if (!name.empty())
    apply_named_style(name.raw());
}

void MainWindow::apply_named_style(const std::string& name)
{
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  buffer_->get_selection_bounds(start_iter, end_iter);
  const int sel_start = start_iter.get_offset();
  const int sel_end = end_iter.get_offset();
  // Every paragraph the selection touches, as for the paragraph format.
  const int first = paragraph_index(sel_start);
  const int last = paragraph_index(sel_end > sel_start ? sel_end - 1 : sel_end);
  const Document before = capture();
  Document after = before;
  adopt_sheet(after, typing_.font, typing_.size);
  const std::vector<Style> styles = complete_sheet(style_sheet(after));
  const size_t caret_para = static_cast<size_t>(paragraph_index(cursor_offset()));
  const Style* from = caret_para < before.paragraphs.size()
                          ? find_style(styles, before.paragraphs[caret_para].style)
                          : nullptr;
  const Style* to = find_style(styles, name);
  if (!apply_style(after, static_cast<size_t>(first), static_cast<size_t>(last), name) ||
      after.paragraphs == before.paragraphs) {
    show_style();
    text_.grab_focus();
    return;
  }
  // What is typed next follows the style too, keeping any direct format.
  if (to)
    restyle_run(typing_, from ? *from : styles.front(), *to);
  commit_document(before, after);
}

void MainWindow::apply_next_style()
{
  if (!next_style_pending_)
    return;
  next_style_pending_ = false;
  const int from = next_style_from_;
  next_style_from_ = -1;
  if (from < 0)
    return;
  Document doc = capture();
  const size_t at = static_cast<size_t>(from) + 1;
  if (at >= doc.paragraphs.size() || paragraph_index(cursor_offset()) != static_cast<int>(at))
    return;
  for (const Run& run : doc.paragraphs[at].runs) {
    if (!run.text.empty())
      return;
  }
  const std::vector<Style> styles = complete_sheet(style_sheet(doc));
  const std::string next = next_style(styles, doc.paragraphs[static_cast<size_t>(from)].style);
  const Style* old = find_style(styles, doc.paragraphs[at].style);
  const Style* to = find_style(styles, next);
  if (!to || old == to)
    return;
  if (!apply_style(doc, at, at, next))
    return;
  // Part of the Enter's own undo step: on_user_end compares against the
  // snapshot taken before it.
  const int caret = cursor_offset();
  const bool was = restoring_;
  restoring_ = true;
  replace_buffer(doc, caret);
  restoring_ = was;
  restyle_run(typing_, old ? *old : styles.front(), *to);
}

void MainWindow::commit_document(const Document& before, const Document& after)
{
  if (before == after) {
    text_.grab_focus();
    return;
  }
  const int insert = buffer_->get_insert()->get_iter().get_offset();
  const int bound = buffer_->get_selection_bound()->get_iter().get_offset();
  if (static_cast<int>(undo_.size()) >= kUndoCap)
    undo_.erase(undo_.begin());
  Snapshot snap;
  snap.doc = before;
  snap.offset = insert;
  undo_.push_back(std::move(snap));
  redo_.clear();
  restoring_ = true;
  replace_buffer(after, insert);
  buffer_->select_range(buffer_->get_iter_at_offset(insert), buffer_->get_iter_at_offset(bound));
  restoring_ = false;
  // A style change is its own undo step, never merged into typing.
  last_typed_us_ = 0;
  update_title();
  update_actions();
  sync_format_controls();
  ruler_.queue_draw();
  text_.queue_draw();
  text_.grab_focus();
}

void MainWindow::on_style_dialog()
{
  // Word 97's Format > Style, cut down: pick a style, change its font and
  // paragraph format, what it is based on and what follows it, or make a new
  // one. OK applies every change as one undo step; each paragraph in a style
  // follows it, attribute by attribute, keeping its direct format.
  const Units units = settings_.units;
  const Document before = capture();
  Document base = before;
  adopt_sheet(base, typing_.font, typing_.size);
  base.styles = complete_sheet(style_sheet(base));
  const std::vector<Style> original = base.styles;
  std::vector<Style> work = original;
  // The styles New... made, as made, so their later edits are changes too.
  std::vector<Style> added;
  size_t current = 0;
  {
    const Style* caret = find_style(work, para_at(cursor_offset()).style);
    if (caret)
      current = static_cast<size_t>(caret - work.data());
  }

  Gtk::Dialog dialog("Style", *this, true);
  dialog.set_resizable(false);
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

  auto* chooser = Gtk::manage(new Gtk::ComboBoxText());
  auto* make = Gtk::manage(new Gtk::Button("_New…", true));
  auto* based = Gtk::manage(new Gtk::ComboBoxText());
  auto* follow = Gtk::manage(new Gtk::ComboBoxText());
  for (Gtk::ComboBoxText* names : {chooser, based, follow}) {
    narrow_name_box(*names);
    name_tooltip(*names);
  }
  auto* font = Gtk::manage(new Gtk::ComboBoxText());
  fill_font_combo(*font, work[current].format.font);
  auto* size =
      Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(11, 1, kMaxStyleSize, 1, 2), 1, 0));
  size->set_activates_default(true);
  auto* bold = Gtk::manage(new Gtk::CheckButton("_Bold", true));
  auto* italic = Gtk::manage(new Gtk::CheckButton("_Italic", true));
  auto* underline = Gtk::manage(new Gtk::CheckButton("_Underline", true));
  auto* alignment = Gtk::manage(new Gtk::ComboBoxText());
  alignment->append("Left");
  alignment->append("Centered");
  alignment->append("Right");
  alignment->append("Justified");
  auto* outline = Gtk::manage(new Gtk::ComboBoxText());
  outline->append("Body text");
  for (int level = 1; level <= 6; ++level)
    outline->append("Level " + std::to_string(level));
  auto* left = measure_spin(units, 0, false);
  auto* right = measure_spin(units, 0, false);
  auto* first = measure_spin(units, 0, true);
  first->set_tooltip_text("Negative hangs the first line");

  int row = 0;
  auto* top = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  top->pack_start(*chooser, Gtk::PACK_EXPAND_WIDGET);
  top->pack_start(*make, Gtk::PACK_SHRINK);
  grid->attach(*field_label("_Style:", *chooser), 0, row, 1, 1);
  grid->attach(*top, 1, row++, 3, 1);
  grid->attach(*field_label("Based _on:", *based), 0, row, 1, 1);
  grid->attach(*based, 1, row, 1, 1);
  grid->attach(*field_label("St_yle for following paragraph:", *follow), 2, row, 1, 1);
  grid->attach(*follow, 3, row++, 1, 1);
  grid->attach(*field_label("_Font:", *font), 0, row, 1, 1);
  grid->attach(*font, 1, row, 1, 1);
  grid->attach(*field_label("Si_ze:", *size), 2, row, 1, 1);
  grid->attach(*size, 3, row++, 1, 1);
  auto* flags = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  flags->pack_start(*bold, Gtk::PACK_SHRINK);
  flags->pack_start(*italic, Gtk::PACK_SHRINK);
  flags->pack_start(*underline, Gtk::PACK_SHRINK);
  grid->attach(*flags, 1, row++, 3, 1);
  grid->attach(*field_label("Ali_gnment:", *alignment), 0, row, 1, 1);
  grid->attach(*alignment, 1, row, 1, 1);
  grid->attach(*field_label("Outline _level:", *outline), 2, row, 1, 1);
  grid->attach(*outline, 3, row++, 1, 1);
  grid->attach(*field_label("_Left indent:", *left), 0, row, 1, 1);
  grid->attach(*left, 1, row, 1, 1);
  grid->attach(*field_label("_Right indent:", *right), 2, row, 1, 1);
  grid->attach(*right, 3, row++, 1, 1);
  grid->attach(*field_label("First l_ine:", *first), 0, row, 1, 1);
  grid->attach(*first, 1, row++, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);

  // What the fields showed for the style on screen, to tell untouched
  // indents (which keep their exact twips) from edited ones.
  double left_shown = 0;
  double right_shown = 0;
  double first_shown = 0;
  bool filling = false;
  auto fill_chooser = [&] {
    filling = true;
    chooser->remove_all();
    for (const Style& style : work)
      chooser->append(style.name);
    chooser->set_active(static_cast<int>(current));
    filling = false;
  };
  auto load = [&](size_t index) {
    filling = true;
    const Style& style = work[index];
    const bool normal = index == 0;
    based->remove_all();
    based->append("(none)");
    int based_row = 0;
    int count = 1;
    for (const Style& other : work) {
      // Nothing based on this style can be its base.
      if (normal || descends_from(work, other.name, style.name))
        continue;
      based->append(other.name);
      if (other.name == style.based_on)
        based_row = count;
      ++count;
    }
    based->set_active(based_row);
    based->set_sensitive(!normal);
    follow->remove_all();
    int follow_row = 0;
    for (size_t i = 0; i < work.size(); ++i) {
      follow->append(work[i].name);
      if (work[i].name == next_style(work, style.name))
        follow_row = static_cast<int>(i);
    }
    follow->set_active(follow_row);
    font->set_active_text(style.format.font);
    if (font->get_active_text() != style.format.font) {
      font->append(style.format.font);
      font->set_active_text(style.format.font);
    }
    size->set_value(style.format.size);
    bold->set_active(style.format.bold);
    italic->set_active(style.format.italic);
    underline->set_active(style.format.underline);
    alignment->set_active(static_cast<int>(style.align));
    outline->set_active(std::max(0, std::min(6, style.heading)));
    left->set_value(units_round(twips_to_units(style.indents.left, units), units));
    right->set_value(units_round(twips_to_units(style.indents.right, units), units));
    first->set_value(units_round(twips_to_units(style.indents.first, units), units));
    left_shown = left->get_value();
    right_shown = right->get_value();
    first_shown = first->get_value();
    filling = false;
  };
  auto store = [&](size_t index) {
    Style& style = work[index];
    left->update();
    right->update();
    first->update();
    size->update();
    const Glib::ustring base_name = based->get_active_text();
    style.based_on = index == 0 || based->get_active_row_number() <= 0 ? "" : base_name.raw();
    const Glib::ustring next_name = follow->get_active_text();
    style.next = next_name.raw() == style.name ? "" : next_name.raw();
    if (!font->get_active_text().empty())
      style.format.font = font->get_active_text().raw();
    style.format.size = std::max(1, std::min(kMaxStyleSize, size->get_value_as_int()));
    style.format.bold = bold->get_active();
    style.format.italic = italic->get_active();
    style.format.underline = underline->get_active();
    style.align = static_cast<Align>(std::max(0, alignment->get_active_row_number()));
    style.heading = std::max(0, outline->get_active_row_number());
    Indents indents;
    indents.left = keep_twips(style.indents.left, left_shown, left->get_value(), units);
    indents.right = keep_twips(style.indents.right, right_shown, right->get_value(), units);
    indents.first = keep_twips(style.indents.first, first_shown, first->get_value(), units);
    style.indents = clamp_indents(indents);
  };
  chooser->signal_changed().connect([&] {
    if (filling || chooser->get_active_row_number() < 0)
      return;
    store(current);
    current = static_cast<size_t>(chooser->get_active_row_number());
    load(current);
  });
  make->signal_clicked().connect([&] {
    store(current);
    Gtk::Dialog ask("New Style", dialog, true);
    ask.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    ask.add_button("_OK", Gtk::RESPONSE_OK);
    ask.set_default_response(Gtk::RESPONSE_OK);
    auto* entry = Gtk::manage(new Gtk::Entry());
    entry->set_activates_default(true);
    entry->set_max_length(static_cast<int>(kMaxStyleName));
    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
    box->set_margin_top(12);
    box->set_margin_bottom(12);
    box->set_margin_start(12);
    box->set_margin_end(12);
    box->pack_start(*field_label("_Name:", *entry), Gtk::PACK_SHRINK);
    box->pack_start(*entry, Gtk::PACK_EXPAND_WIDGET);
    ask.get_content_area()->pack_start(*box, Gtk::PACK_SHRINK);
    ask.show_all_children();
    for (;;) {
      if (ask.run() != Gtk::RESPONSE_OK)
        return;
      const std::string name = clean_style_name(entry->get_text().raw());
      const char* problem = nullptr;
      if (name.empty())
        problem = "A style needs a name.";
      else if (find_style(work, name))
        problem = "There is already a style with that name.";
      else if (work.size() >= kMaxStyles)
        problem = "The document has as many styles as it can hold.";
      if (!problem) {
        // Word's new style: the chosen one's format, based on it, followed
        // by itself.
        Style style = work[current];
        style.name = name;
        style.based_on = work[current].name;
        style.next.clear();
        work.push_back(style);
        added.push_back(style);
        current = work.size() - 1;
        fill_chooser();
        load(current);
        return;
      }
      Gtk::MessageDialog message(ask, problem, false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
      message.set_title("New Style");
      message.run();
      message.hide();
      entry->grab_focus();
    }
  });
  fill_chooser();
  load(current);
  dialog.show_all_children();
  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      text_.grab_focus();
      return;
    }
    store(current);
    Document after = base;
    std::string failed;
    // New styles first, following themselves for now, as their next style
    // may be newer still; their real next lands with the changes below.
    for (const Style& made : added) {
      Style plain = made;
      plain.next.clear();
      if (!add_style(after, plain)) {
        failed = made.name;
        break;
      }
    }
    for (size_t i = 0; i < work.size() && failed.empty(); ++i) {
      const Style& edited = work[i];
      const Style* shown = nullptr;
      if (i < original.size()) {
        shown = &original[i];
      } else {
        for (const Style& made : added) {
          if (made.name == edited.name)
            shown = &made;
        }
      }
      Style blank_next;
      if (i >= original.size() && shown) {
        // Compared with what add_style() was given, so a next style is a
        // change to make.
        blank_next = *shown;
        blank_next.next.clear();
        shown = &blank_next;
      }
      if (!shown || edited == *shown)
        continue;
      const Style* now = find_style(after.styles, edited.name);
      if (!now || !update_style(after, edited.name, merge_edit(*now, *shown, edited)))
        failed = edited.name;
    }
    if (failed.empty()) {
      if (added.empty() && work == original) {
        text_.grab_focus();
        return;
      }
      commit_document(before, after);
      return;
    }
    Gtk::MessageDialog message(dialog,
                               "The style \"" + failed + "\" could not be changed that way.", false,
                               Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
    message.set_secondary_text(
        "A style cannot be based on itself or on a style based on it, and its name must be its "
        "own.");
    message.set_title("Style");
    message.run();
    message.hide();
  }
}

}  // namespace writeit
