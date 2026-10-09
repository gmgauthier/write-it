/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. A list label wider than its hang, such
// as "32767.", must not run into the item's text: as in Word 97, a left-
// aligned or justified item's text then starts at the next default tab
// stop (every half inch from the left margin) clear of the label.
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
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <utility>

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
  static long evaluated(MainWindow& w)
  {
    return w.list_tabs_evaluated_;
  }
  static long updates(MainWindow& w)
  {
    return w.list_updates_;
  }
  // Deletes the paragraph break at the end of paragraph `line`, as Delete
  // does there: the next item's text joins this one.
  static void delete_break(MainWindow& w, int line)
  {
    auto start = w.buffer_->get_iter_at_line(line);
    start.forward_to_line_end();
    auto end = start;
    end.forward_char();
    w.buffer_->begin_user_action();
    w.buffer_->erase_interactive(start, end, true);
    w.buffer_->end_user_action();
  }
  // Enter at the end of paragraph `line`, as one user action.
  static void enter_at_end(MainWindow& w, int line)
  {
    type_at_end(w, line, "\n");
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
  // The label's end and a pixel clear of it.
  const int end = label_x + MainWindowProbe::width(w, "32767.") + 1;
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

// Where paragraph `line` of a standard first-level list item, labelled
// `label`, must start: at the hang, or at the next stop clear of the label.
int want_x(writeit::MainWindow& w, const std::string& label)
{
  // Widths by zoom and label: measuring 2000 labels each time is slow
  // under ASan.
  static std::map<std::pair<int, std::string>, int> widths;
  const auto key = std::make_pair(MainWindowProbe::px(w, 1440), label);
  auto known = widths.find(key);
  if (known == widths.end())
    known = widths.emplace(key, MainWindowProbe::width(w, label.c_str())).first;
  const writeit::Indents in = writeit::list_indents(0);
  const int end = MainWindowProbe::px(w, in.left + in.first) + known->second + 1;
  const int hang = MainWindowProbe::px(w, in.left);
  return end <= hang ? hang : next_stop(w, end);
}

// Every item of the long list sits where its label puts it.
bool all_placed(writeit::MainWindow& w, int items)
{
  for (int i = 0; i < items; ++i) {
    const std::string label = std::to_string(i + 1) + ".";
    if (MainWindowProbe::text_x(w, i) != want_x(w, label)) {
      std::cout << "  item " << i << " at " << MainWindowProbe::text_x(w, i) << ", want "
                << want_x(w, label) << "\n";
      return false;
    }
  }
  return true;
}

// Nothing changes while the window sits idle: no list update runs, so a
// list on screen costs no CPU. Two seconds of the main loop, idles and
// timers included.
void idle(writeit::MainWindow& w, const char* what)
{
  settle();
  const long before = MainWindowProbe::updates(w);
  const std::clock_t cpu = std::clock();
  const gint64 until = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
  auto context = Glib::MainContext::get_default();
  while (g_get_monotonic_time() < until) {
    if (context->pending())
      context->iteration(false);
    else
      g_usleep(10000);
  }
  const double used = static_cast<double>(std::clock() - cpu) / CLOCKS_PER_SEC;
  std::cout << "  idle 2 s, " << what << ": " << MainWindowProbe::updates(w) - before
            << " list updates, " << used << " s CPU\n";
  CHECK(MainWindowProbe::updates(w) == before);
}

// Renumbering across 9 and 10 both ways: Enter makes a tenth item, whose
// wider label sends its text to the next stop; Delete of a paragraph break
// takes it back to nine, and "9." lets its text back to the hang. Undo
// either way puts the other back.
void crossing(const std::string& home)
{
  writeit::Document doc;
  for (int i = 0; i < 9; ++i)
    doc.paragraphs.push_back(numbered("Item", 1, 1));
  const std::string path = Glib::build_filename(home, "nine.rtf");
  Glib::file_set_contents(path, writeit::rtf_export(doc));
  writeit::MainWindow window;
  window.show();
  settle();
  MainWindowProbe::open(window, path);
  settle();
  const writeit::Indents in = writeit::list_indents(0);
  const int hang = MainWindowProbe::px(window, in.left);
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  idle(window, "nine items");

  // 9 to 10 with Enter.
  MainWindowProbe::enter_at_end(window, 2);
  settle();
  CHECK(all_placed(window, 10) && MainWindowProbe::text_x(window, 9) > hang);
  // 10 to 9 with Delete.
  MainWindowProbe::delete_break(window, 2);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  // And back with undo: 9 to 10, then Enter's undo, 10 to 9.
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, 10) && MainWindowProbe::text_x(window, 9) > hang);
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  // 10 to 9 with Delete at an item's own break, the tenth's text joining.
  MainWindowProbe::enter_at_end(window, 8);
  settle();
  CHECK(all_placed(window, 10) && MainWindowProbe::text_x(window, 9) > hang);
  MainWindowProbe::delete_break(window, 8);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  window.hide();
  settle();
  g_remove(path.c_str());
}

// A long list: typing in one item looks again at that item only, not the
// whole document, while Enter (which renumbers the items below), undo and
// zoom still leave every item's text in the right place.
void long_list(const std::string& home)
{
  constexpr int kItems = 2000;
  writeit::Document doc;
  for (int i = 0; i < kItems; ++i)
    doc.paragraphs.push_back(numbered("Item", 1, 1));
  const std::string path = Glib::build_filename(home, "long.rtf");
  Glib::file_set_contents(path, writeit::rtf_export(doc));
  writeit::MainWindow window;
  window.show();
  settle();
  MainWindowProbe::open(window, path);
  settle();
  CHECK(all_placed(window, kItems));

  // One keystroke in item 1000, then one in item 5: O(1) paragraphs each.
  for (int line : {999, 4}) {
    const long before = MainWindowProbe::evaluated(window);
    MainWindowProbe::type_at_end(window, line, "x");
    settle();
    const long looked = MainWindowProbe::evaluated(window) - before;
    std::cout << "  typing in item " << line + 1 << ": " << looked << " paragraphs\n";
    CHECK(looked <= 3);
  }
  CHECK(all_placed(window, kItems));

  // Enter after item 9 makes a new item 10: it and every item below move
  // to their new numbers' places.
  MainWindowProbe::enter_at_end(window, 8);
  settle();
  CHECK(all_placed(window, kItems + 1));
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, kItems));

  MainWindowProbe::zoom(window, 200);
  settle();
  CHECK(all_placed(window, kItems));
  MainWindowProbe::zoom(window, 100);
  settle();
  idle(window, "2000 items");
  window.hide();
  settle();
  g_remove(path.c_str());
}

}  // namespace

constexpr int kChecks = 53;

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

  crossing(home);
  long_list(home);

  g_remove(path.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("list-tabs", kChecks);
}
