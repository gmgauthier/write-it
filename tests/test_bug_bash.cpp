/* SPDX-License-Identifier: Unlicense */

// Paragraph commands follow the cell or the story that has the keyboard.
// Replace in a cell keeps the match's character format. A run of keys in a
// cell or a header is one undo step. Find Next moves on from a note match.
// A note's space before is drawn. Deleting a footnote marker drops that note.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <iostream>
#include <string>

namespace writeit {

struct MainWindowProbe {
  static void zoom(MainWindow& window, int percent)
  {
    window.set_zoom(percent);
  }

  static void load(MainWindow& window, const Document& doc)
  {
    window.undo_.clear();
    window.replace_buffer(doc, 0);
    window.saved_state_ = window.undo_.state_id();
    window.save_point_ = true;
    window.last_typed_us_ = 0;
    window.update_title();
    window.update_actions();
    window.sync_format_controls();
  }

  static void focus_cell(MainWindow& window)
  {
    Gtk::TextView* view = window.nth_cell_view(0, 0, 0);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->get_char_count() > 0 ? buffer->end() : buffer->begin());
  }

  static void focus_body(MainWindow& window)
  {
    window.text_.grab_focus();
  }

  static void focus_header(MainWindow& window)
  {
    window.header_view_.grab_focus();
  }

  static void focus_note(MainWindow& window)
  {
    window.notes_view_.grab_focus();
  }

  static bool cell_edit_on(const MainWindow& window)
  {
    return window.cell_edit_.on;
  }

  static void align(MainWindow& window, Align align)
  {
    window.apply_align(align);
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }

  static ParaFormat focused(MainWindow& window)
  {
    return window.focused_para();
  }

  static Gtk::TextView* cell(MainWindow& window)
  {
    return window.nth_cell_view(0, 0, 0);
  }

  static void select_word(Gtk::TextView& view, const char* word)
  {
    auto buffer = view.get_buffer();
    Gtk::TextBuffer::iterator start;
    Gtk::TextBuffer::iterator end;
    if (!buffer->begin().forward_search(word, Gtk::TEXT_SEARCH_TEXT_ONLY, start, end))
      return;
    buffer->select_range(start, end);
  }

  static void find_ready(MainWindow& window, const char* needle, const char* replacement)
  {
    if (!window.find_entry_)
      window.build_find();
    window.find_entry_->set_text(needle);
    if (replacement)
      window.replace_entry_->set_text(replacement);
  }

  static void replace(MainWindow& window)
  {
    window.replace_once();
  }

  static void find_next(MainWindow& window)
  {
    window.find_next();
  }

  static std::size_t undo_steps(const MainWindow& window)
  {
    return window.undo_.size();
  }

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static void type_key(Gtk::TextView& view, const char* text)
  {
    auto buffer = view.get_buffer();
    buffer->begin_user_action();
    buffer->insert_at_cursor(text);
    buffer->end_user_action();
  }

  static bool erase_note(MainWindow& window, int number)
  {
    auto buffer = window.buffer_;
    for (auto at = buffer->begin(); !at.is_end(); at.forward_char()) {
      if (window.format_of(at).note != number)
        continue;
      auto end = at;
      end.forward_char();
      buffer->begin_user_action();
      buffer->erase(at, end);
      buffer->end_user_action();
      return true;
    }
    return false;
  }

  static int pixels_above(Gtk::TextView& view)
  {
    auto buffer = view.get_buffer();
    if (!buffer || buffer->get_char_count() == 0)
      return -1;
    GtkTextAttributes* attrs = gtk_text_attributes_new();
    gtk_text_iter_get_attributes(buffer->begin().gobj(), attrs);
    const int above = attrs->pixels_above_lines;
    gtk_text_attributes_unref(attrs);
    return above;
  }

  static int selection_start(Gtk::TextView& view)
  {
    auto buffer = view.get_buffer();
    Gtk::TextIter start;
    Gtk::TextIter end;
    if (!buffer->get_selection_bounds(start, end))
      return -1;
    return start.get_offset();
  }

  static void type_header(MainWindow& window, const char* text)
  {
    type_key(window.header_view_, text);
  }

  static int note_above(MainWindow& window)
  {
    return pixels_above(window.notes_view_);
  }

  static void place_note_start(MainWindow& window)
  {
    auto buffer = window.notes_view_.get_buffer();
    if (buffer)
      buffer->place_cursor(buffer->begin());
  }

  static int note_selection(MainWindow& window)
  {
    return selection_start(window.notes_view_);
  }
};

}  // namespace writeit

namespace {

using writeit::Align;
using writeit::Document;
using writeit::MainWindowProbe;
using writeit::Paragraph;
using writeit::Run;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 6; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

Document cell_doc(const char* cell_text, bool bold)
{
  Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs.clear();
  Paragraph before;
  Run body;
  body.font = "Sans";
  body.size = 11;
  body.text = "Before";
  before.runs.push_back(body);
  doc.paragraphs.push_back(before);
  writeit::insert_table(doc.paragraphs, 1, 1, 1, writeit::page_text_twips(doc.page));
  Run cell;
  cell.font = "Sans";
  cell.size = 11;
  cell.bold = bold;
  cell.text = cell_text;
  doc.paragraphs[1].runs = {cell};
  Paragraph after;
  Run after_run;
  after_run.font = "Sans";
  after_run.size = 11;
  after_run.text = "After";
  after.runs.push_back(after_run);
  doc.paragraphs.push_back(after);
  return doc;
}

const Paragraph* cell_paragraph(const Document& doc)
{
  for (const Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      return &paragraph;
  }
  return nullptr;
}

const Paragraph* body_named(const Document& doc, const char* text)
{
  for (const Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      continue;
    for (const Run& run : paragraph.runs) {
      if (run.text == text)
        return &paragraph;
    }
  }
  return nullptr;
}

std::string runs_text(const Paragraph& paragraph)
{
  std::string out;
  for (const Run& run : paragraph.runs)
    out += run.text;
  return out;
}

Document story_doc()
{
  Document doc = writeit::blank_document("Sans", 11);
  Run body;
  body.font = "Sans";
  body.size = 11;
  body.text = "alpha";
  doc.paragraphs[0].runs.push_back(body);
  Paragraph head;
  Run head_run;
  head_run.font = "Sans";
  head_run.size = 11;
  head_run.text = "Head";
  head.runs.push_back(head_run);
  doc.header.push_back(head);
  Paragraph note;
  note.spacing.before = 240;
  Run note_run;
  note_run.font = "Sans";
  note_run.size = 11;
  note_run.text = "alpha alpha";
  note.runs.push_back(note_run);
  doc.notes.push_back({note});
  Run mark;
  mark.font = "Sans";
  mark.size = 11;
  mark.text = "1";
  mark.note = 1;
  doc.paragraphs[0].runs.push_back(mark);
  return doc;
}

Document notes_doc()
{
  Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs.clear();
  Paragraph paragraph;
  Run see;
  see.font = "Sans";
  see.size = 11;
  see.text = "See ";
  Run first;
  first.font = "Sans";
  first.size = 11;
  first.text = "1";
  first.note = 1;
  Run mid;
  mid.font = "Sans";
  mid.size = 11;
  mid.text = " and ";
  Run second;
  second.font = "Sans";
  second.size = 11;
  second.text = "2";
  second.note = 2;
  paragraph.runs = {see, first, mid, second};
  doc.paragraphs.push_back(paragraph);
  auto story = [](const char* text) {
    Paragraph note;
    Run run;
    run.font = "Sans";
    run.size = 11;
    run.text = text;
    note.runs.push_back(run);
    return note;
  };
  doc.notes.push_back({story("First note")});
  doc.notes.push_back({story("Second note")});
  return doc;
}

int markers(const Document& doc, int& number, std::string& text)
{
  int count = 0;
  number = 0;
  text.clear();
  for (const Paragraph& paragraph : doc.paragraphs) {
    for (const Run& run : paragraph.runs) {
      if (run.note == 0)
        continue;
      ++count;
      number = run.note;
      text = run.text;
    }
  }
  return count;
}

}  // namespace

constexpr int kChecks = 31;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-bug-bash-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "bug-bash: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    window.resize(900, 700);
    settle();
    MainWindowProbe::zoom(window, 100);
    settle();

    MainWindowProbe::load(window, cell_doc("Boldword", true));
    settle();
    MainWindowProbe::focus_cell(window);
    settle();
    MainWindowProbe::focus_body(window);
    settle();
    CHECK(MainWindowProbe::cell_edit_on(window));
    MainWindowProbe::align(window, Align::Center);
    settle();
    const Document centered = MainWindowProbe::doc(window);
    const Paragraph* cell = cell_paragraph(centered);
    const Paragraph* before = body_named(centered, "Before");
    const Paragraph* after = body_named(centered, "After");
    CHECK(cell && cell->align == Align::Center);
    CHECK(before && before->align == Align::Left);
    CHECK(after && after->align == Align::Left);
    CHECK(MainWindowProbe::focused(window).align == Align::Center);

    Gtk::TextView* cell_view = MainWindowProbe::cell(window);
    CHECK(cell_view != nullptr);
    cell_view->grab_focus();
    settle();
    MainWindowProbe::select_word(*cell_view, "Boldword");
    MainWindowProbe::find_ready(window, "Boldword", "Kept");
    MainWindowProbe::replace(window);
    settle();
    const Document replaced = MainWindowProbe::doc(window);
    const Paragraph* kept = cell_paragraph(replaced);
    CHECK(kept && runs_text(*kept) == "Kept");
    CHECK(kept && !kept->runs.empty() && kept->runs[0].bold);
    CHECK(body_named(replaced, "Before") != nullptr);

    MainWindowProbe::load(window, cell_doc("Cell", false));
    settle();
    MainWindowProbe::focus_cell(window);
    settle();
    Gtk::TextView* typing = MainWindowProbe::cell(window);
    CHECK(typing != nullptr);
    MainWindowProbe::type_key(*typing, "a");
    settle();
    MainWindowProbe::type_key(*typing, "b");
    settle();
    CHECK(MainWindowProbe::undo_steps(window) == 1);
    const Document typed_doc = MainWindowProbe::doc(window);
    const Paragraph* typed = cell_paragraph(typed_doc);
    CHECK(typed && runs_text(*typed) == "Cellab");
    MainWindowProbe::undo(window);
    settle();
    const Document restored_doc = MainWindowProbe::doc(window);
    const Paragraph* restored = cell_paragraph(restored_doc);
    CHECK(restored && runs_text(*restored) == "Cell");

    MainWindowProbe::load(window, story_doc());
    settle();
    MainWindowProbe::focus_header(window);
    settle();
    const std::size_t align_steps = MainWindowProbe::undo_steps(window);
    MainWindowProbe::align(window, Align::Right);
    settle();
    const Document headed = MainWindowProbe::doc(window);
    CHECK(!headed.header.empty() && headed.header[0].align == Align::Right);
    CHECK(headed.paragraphs[0].align == Align::Left);
    CHECK(MainWindowProbe::focused(window).align == Align::Right);
    CHECK(MainWindowProbe::undo_steps(window) == align_steps + 1);
    MainWindowProbe::type_header(window, "x");
    settle();
    MainWindowProbe::type_header(window, "y");
    settle();
    CHECK(MainWindowProbe::undo_steps(window) == align_steps + 2);
    MainWindowProbe::undo(window);
    settle();
    const Document untyped = MainWindowProbe::doc(window);
    CHECK(!untyped.header.empty() && runs_text(untyped.header[0]) == "Head");
    CHECK(!untyped.header.empty() && untyped.header[0].align == Align::Right);

    MainWindowProbe::focus_note(window);
    settle();
    CHECK(MainWindowProbe::note_above(window) == 11);
    MainWindowProbe::align(window, Align::Center);
    settle();
    const Document noted = MainWindowProbe::doc(window);
    CHECK(!noted.notes.empty() && !noted.notes[0].empty() &&
          noted.notes[0][0].align == Align::Center);
    CHECK(noted.paragraphs[0].align == Align::Left);
    MainWindowProbe::focus_note(window);
    settle();
    MainWindowProbe::place_note_start(window);
    MainWindowProbe::find_ready(window, "alpha", nullptr);
    MainWindowProbe::find_next(window);
    settle();
    CHECK(MainWindowProbe::note_selection(window) == 0);
    MainWindowProbe::find_next(window);
    settle();
    CHECK(MainWindowProbe::note_selection(window) == 6);

    MainWindowProbe::load(window, notes_doc());
    settle();
    CHECK(MainWindowProbe::erase_note(window, 1));
    settle();
    const Document dropped = MainWindowProbe::doc(window);
    int number = 0;
    std::string mark;
    CHECK(dropped.notes.size() == 1);
    CHECK(dropped.notes.size() == 1 && !dropped.notes[0].empty() &&
          runs_text(dropped.notes[0][0]) == "Second note");
    CHECK(markers(dropped, number, mark) == 1);
    CHECK(number == 1 && mark == "1");
    MainWindowProbe::undo(window);
    settle();
    CHECK(MainWindowProbe::doc(window).notes.size() == 2);
  }
  return suite_test::done("bug-bash", kChecks);
}
