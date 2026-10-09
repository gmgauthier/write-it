/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. A list label wider than its hang, such
// as "32767.", must not run into the item's text: as in Word 97, a left-
// aligned or justified item's text then starts at the next default tab
// stop (every half inch from the left margin) past the label and a space.
// The move is on screen only: never in the document, the file, or dirty.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static void open(MainWindow& w, const std::string& path)
  {
    w.open_path(path, MainWindow::OpenKind::Rtf);
  }
  static void zoom(MainWindow& w, int percent)
  {
    for (int i = 0; i < 6; ++i)
      if (kZoomChoices[i] == percent)
        w.zoom_view_[i]->activate();
  }
  // Buffer x of a paragraph's first character.
  static int text_x(MainWindow& w, int line)
  {
    Gdk::Rectangle rect;
    w.text_.get_iter_location(w.buffer_->get_iter_at_line(line), rect);
    return rect.get_x();
  }
  static int px(MainWindow& w, int twips)
  {
    return w.margin_left() + w.indent_px(twips);
  }
  // The pixel width of `text` in Sans 11 at the window's zoom, as the label.
  static int width(MainWindow& w, const char* text)
  {
    auto layout = w.text_.create_pango_layout(text);
    Pango::FontDescription desc;
    desc.set_family("Sans");
    desc.set_size(static_cast<int>(11 * w.zoom_factor() * PANGO_SCALE));
    layout->set_font_description(desc);
    int width = 0;
    int height = 0;
    layout->get_pixel_size(width, height);
    return width;
  }
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  static bool dirty(MainWindow& w)
  {
    return w.dirty();
  }
  static void undo(MainWindow& w)
  {
    w.undo();
  }
  // Types `text` at the end of paragraph `line`, as one user action.
  static void type_at_end(MainWindow& w, int line, const char* text)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    iter.forward_to_line_end();
    w.buffer_->place_cursor(iter);
    w.buffer_->begin_user_action();
    w.buffer_->insert_interactive_at_cursor(text, true);
    w.buffer_->end_user_action();
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

constexpr int kWide = 0;     // left-aligned, "32767."
constexpr int kNarrow = 1;   // left-aligned, "1."
constexpr int kJustify = 2;  // justified, "32767."
constexpr int kRoomy = 3;    // left-aligned, "32767." with a 1" hang

writeit::Paragraph numbered(const char* text, int list, int start)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  paragraph.list.kind = writeit::ListKind::Number;
  paragraph.list.list = list;
  paragraph.list.start = start;
  paragraph.indents = writeit::list_indents(0);
  return paragraph;
}

writeit::Document items()
{
  writeit::Document doc;
  doc.paragraphs.push_back(numbered("Wide label on a left-aligned item.", 1, 32767));
  doc.paragraphs.push_back(numbered("Narrow label.", 2, 1));
  doc.paragraphs.push_back(numbered("Wide label on a justified item.", 3, 32767));
  doc.paragraphs.back().align = writeit::Align::Justify;
  doc.paragraphs.push_back(numbered("Wide label with room for it.", 4, 32767));
  doc.paragraphs.back().indents = writeit::Indents{1440, 0, -1440};
  return doc;
}

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

// The first half-inch stop from the left margin at or past `end` px.
int next_stop(writeit::MainWindow& w, int end)
{
  int stop = 0;
  while (MainWindowProbe::px(w, stop) < end)
    stop += 720;
  return MainWindowProbe::px(w, stop);
}

void expect_tab(writeit::MainWindow& w, int line, int percent)
{
  const writeit::Indents in = writeit::list_indents(0);
  const int label_x = MainWindowProbe::px(w, in.left + in.first);
  const int end = label_x + MainWindowProbe::width(w, "32767.") + MainWindowProbe::width(w, " ");
  const int x = MainWindowProbe::text_x(w, line);
  std::cout << "  " << percent << "% line " << line << ": label " << label_x << ".." << end
            << " text " << x << " stop " << next_stop(w, end) << "\n";
  CHECK(x >= end);
  CHECK(x == next_stop(w, end));
}

void expect(writeit::MainWindow& w, int percent)
{
  MainWindowProbe::zoom(w, percent);
  settle();
  expect_tab(w, kWide, percent);
  expect_tab(w, kJustify, percent);
  // The standard hang is narrower than the label, so the text moved.
  CHECK(MainWindowProbe::text_x(w, kWide) > MainWindowProbe::px(w, 720));
  // A label that fits leaves the text at the hang, as before.
  CHECK(MainWindowProbe::text_x(w, kNarrow) == MainWindowProbe::px(w, 720));
  CHECK(MainWindowProbe::text_x(w, kRoomy) == MainWindowProbe::px(w, 1440));
}

}  // namespace

constexpr int kChecks = 37;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-list-tabs-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "list-tabs: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  const std::string path = Glib::build_filename(home, "tabs.rtf");
  const writeit::Document want = items();
  Glib::file_set_contents(path, writeit::rtf_export(want));

  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, path);
    settle();
    for (int percent : {100, 200, 50, 100})
      expect(window, percent);

    // On screen only: the document, its file, and dirty never see it.
    const writeit::Document seen = MainWindowProbe::doc(window);
    CHECK(seen.paragraphs.size() == want.paragraphs.size());
    CHECK(seen.paragraphs[kWide].indents == want.paragraphs[kWide].indents);
    CHECK(writeit::rtf_export(seen) == writeit::rtf_export(want));
    CHECK(!MainWindowProbe::dirty(window));

    // Typing keeps the text at the stop.
    MainWindowProbe::type_at_end(window, kWide, " More");
    settle();
    expect_tab(window, kWide, 100);
    // Undo takes the typing back and the text stays at the stop.
    MainWindowProbe::undo(window);
    settle();
    expect_tab(window, kWide, 100);
    CHECK(writeit::rtf_export(MainWindowProbe::doc(window)) == writeit::rtf_export(want) &&
          !MainWindowProbe::dirty(window));
    window.hide();
    settle();
  }

  g_remove(path.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("list-tabs", kChecks);
}
