/* SPDX-License-Identifier: Unlicense */

// Focusing the header, the footer, or a note drops the cell that was being
// edited. Focusing the body, as the Table menu does, does not.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <string>

namespace writeit {

struct MainWindowProbe {
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

  static void focus_table_cell(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->get_char_count() > 0 ? buffer->end() : buffer->begin());
  }

  static void focus_body_quietly(MainWindow& window)
  {
    window.text_.grab_focus();
  }

  static bool cell_focused(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    return view && view->has_focus();
  }

  static bool cell_edit_on(const MainWindow& window)
  {
    return window.cell_edit_.on;
  }

  static bool in_table(const MainWindow& window)
  {
    return window.caret_in_table();
  }

  static bool header_shown(const MainWindow& window)
  {
    return window.header_view_.get_visible() && window.header_view_.get_parent() != nullptr;
  }

  static bool footer_shown(const MainWindow& window)
  {
    return window.footer_view_.get_visible() && window.footer_view_.get_parent() != nullptr;
  }

  static bool note_shown(const MainWindow& window)
  {
    return window.notes_view_.get_visible();
  }

  static void focus_header(MainWindow& window)
  {
    window.header_view_.grab_focus();
  }

  static void focus_footer(MainWindow& window)
  {
    window.footer_view_.grab_focus();
  }

  static void focus_note(MainWindow& window)
  {
    window.notes_view_.grab_focus();
  }

  static bool header_focused(const MainWindow& window)
  {
    return window.header_view_.has_focus();
  }

  static bool footer_focused(const MainWindow& window)
  {
    return window.footer_view_.has_focus();
  }

  static bool note_focused(const MainWindow& window)
  {
    return window.notes_view_.has_focus();
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }
};

}  // namespace writeit

namespace {

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 4; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(10000);
  }
  while (context->pending())
    context->iteration(false);
}

void predicates()
{
  CHECK(writeit::story_focus_clears_cell(true, false, false));
  CHECK(writeit::story_focus_clears_cell(false, true, false));
  CHECK(writeit::story_focus_clears_cell(false, false, true));
  CHECK(writeit::story_focus_clears_cell(true, true, true));
  CHECK(!writeit::story_focus_clears_cell(false, false, false));
}

std::string cell_text(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0)
      continue;
    std::string out;
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
    return out;
  }
  return {};
}

void arm(writeit::MainWindow& window)
{
  writeit::MainWindowProbe::focus_table_cell(window, 0, 0, 0);
  settle();
  writeit::MainWindowProbe::focus_body_quietly(window);
  settle();
}

}  // namespace

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-cell-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "cell-edit-sticky: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  predicates();
  {
    writeit::MainWindow window;
    window.show();
    window.resize(900, 700);
    settle();

    writeit::Document doc;
    writeit::Paragraph before;
    writeit::Run body;
    body.text = "Before";
    before.runs.push_back(body);
    doc.paragraphs.push_back(before);
    writeit::insert_table(doc.paragraphs, 1, 1, 1, writeit::page_text_twips(doc.page));
    writeit::Run cell;
    cell.text = "Cell";
    doc.paragraphs[1].runs.push_back(cell);
    writeit::Paragraph head;
    writeit::Run head_run;
    head_run.text = "Head";
    head.runs.push_back(head_run);
    doc.header.push_back(head);
    writeit::Paragraph foot;
    writeit::Run foot_run;
    foot_run.text = "Foot";
    foot.runs.push_back(foot_run);
    doc.footer.push_back(foot);
    writeit::Paragraph note;
    writeit::Run note_run;
    note_run.text = "Note";
    note.runs.push_back(note_run);
    doc.notes.push_back({note});

    writeit::MainWindowProbe::load(window, doc);
    settle();
    CHECK(writeit::MainWindowProbe::header_shown(window));
    CHECK(writeit::MainWindowProbe::footer_shown(window));
    CHECK(writeit::MainWindowProbe::note_shown(window));

    arm(window);
    CHECK(!writeit::MainWindowProbe::cell_focused(window, 0, 0, 0));
    CHECK(writeit::MainWindowProbe::cell_edit_on(window));
    CHECK(writeit::MainWindowProbe::in_table(window));

    writeit::MainWindowProbe::focus_header(window);
    settle();
    CHECK(writeit::MainWindowProbe::header_focused(window));
    CHECK(!writeit::MainWindowProbe::cell_edit_on(window));
    CHECK(!writeit::MainWindowProbe::in_table(window));

    arm(window);
    CHECK(writeit::MainWindowProbe::cell_edit_on(window));
    writeit::MainWindowProbe::focus_footer(window);
    settle();
    CHECK(writeit::MainWindowProbe::footer_focused(window));
    CHECK(!writeit::MainWindowProbe::cell_edit_on(window));
    CHECK(!writeit::MainWindowProbe::in_table(window));

    arm(window);
    CHECK(writeit::MainWindowProbe::cell_edit_on(window));
    writeit::MainWindowProbe::focus_note(window);
    settle();
    CHECK(writeit::MainWindowProbe::note_focused(window));
    CHECK(!writeit::MainWindowProbe::cell_edit_on(window));
    CHECK(!writeit::MainWindowProbe::in_table(window));
    CHECK(cell_text(writeit::MainWindowProbe::doc(window)) == "Cell");
  }
  return suite_test::done("cell-edit-sticky", 23);
}
