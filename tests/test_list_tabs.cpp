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
  // The pixel width of `text` in Sans `size` at the window's zoom, as the
  // label.
  static int width(MainWindow& w, const char* text, int size = 11)
  {
    auto layout = w.text_.create_pango_layout(text);
    Pango::FontDescription desc;
    desc.set_family("Sans");
    desc.set_size(static_cast<int>(size * w.zoom_factor() * PANGO_SCALE));
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
  // Backspace at the start of paragraph `line`, as one user action.
  static void backspace_at_start(MainWindow& w, int line)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    w.buffer_->place_cursor(iter);
    w.buffer_->begin_user_action();
    w.buffer_->backspace(iter, true, true);
    w.buffer_->end_user_action();
  }
  // Deletes paragraphs `first` to `last` whole, as one user action.
  static void delete_items(MainWindow& w, int first, int last)
  {
    w.buffer_->begin_user_action();
    w.buffer_->erase_interactive(w.buffer_->get_iter_at_line(first),
                                 w.buffer_->get_iter_at_line(last + 1), true);
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

writeit::Paragraph numbered(const char* text, int list, int start, int size = 11)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  run.size = size;
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
int want_x(writeit::MainWindow& w, const std::string& label, int size = 11)
{
  // Widths by zoom, size and label: measuring 2000 labels each time is slow
  // under ASan.
  static std::map<std::pair<int, std::string>, int> widths;
  const auto key =
      std::make_pair(MainWindowProbe::px(w, 1440), std::to_string(size) + ' ' + label);
  auto known = widths.find(key);
  if (known == widths.end())
    known = widths.emplace(key, MainWindowProbe::width(w, label.c_str(), size)).first;
  const writeit::Indents in = writeit::list_indents(0);
  const int end = MainWindowProbe::px(w, in.left + in.first) + known->second + 1;
  const int hang = MainWindowProbe::px(w, in.left);
  return end <= hang ? hang : next_stop(w, end);
}

// Every item of a list from the top, numbered from 1 in Sans `size`, sits
// where its label puts it.
bool all_placed(writeit::MainWindow& w, int items, int size = 11)
{
  for (int i = 0; i < items; ++i) {
    const std::string label = std::to_string(i + 1) + ".";
    if (MainWindowProbe::text_x(w, i) != want_x(w, label, size)) {
      std::cout << "  item " << i << " at " << MainWindowProbe::text_x(w, i) << ", want "
                << want_x(w, label, size) << "\n";
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
  // A tenth item with text, then Delete at the ninth's break: the tenth's
  // text joins the ninth, whose label is narrow again.
  auto tenth = [&](int at) {
    MainWindowProbe::enter_at_end(window, at);
    MainWindowProbe::type_at_end(window, at + 1, "Item");
    settle();
  };
  tenth(8);
  CHECK(all_placed(window, 10) && MainWindowProbe::text_x(window, 9) > hang);
  MainWindowProbe::delete_break(window, 8);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  // Backspace at the start of the tenth, and of an item higher up, whose
  // join renumbers every item below it.
  tenth(8);
  MainWindowProbe::backspace_at_start(window, 9);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  tenth(8);
  MainWindowProbe::backspace_at_start(window, 3);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  // Eleven items, then two deleted whole: 11 to 9, and back with undo.
  tenth(8);
  tenth(9);
  CHECK(all_placed(window, 11) && MainWindowProbe::text_x(window, 10) > hang);
  MainWindowProbe::delete_items(window, 3, 4);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, 11) && MainWindowProbe::text_x(window, 10) > hang);
  // Backspace twice high up: 11 to 10, the wide labels moving up one, then 9.
  MainWindowProbe::backspace_at_start(window, 2);
  settle();
  CHECK(all_placed(window, 10) && MainWindowProbe::text_x(window, 9) > hang);
  MainWindowProbe::backspace_at_start(window, 2);
  settle();
  CHECK(all_placed(window, 9) && MainWindowProbe::text_x(window, 8) == hang);
  window.hide();
  settle();
  g_remove(path.c_str());
}

// A Sans size at which items labelled `before` and `after` sit at different
// places under the font in use, so a renumber from one to the other must
// move the text. 0 if none.
int boundary_size(writeit::MainWindow& w, const std::string& before, const std::string& after)
{
  for (int size = 8; size <= 72; ++size)
    if (want_x(w, before, size) != want_x(w, after, size))
      return size;
  return 0;
}

// Opens `items` numbered items from 1 in Sans `size`, then a plain paragraph.
void open_list(writeit::MainWindow& w, const std::string& path, int items, int size)
{
  writeit::Document doc;
  for (int i = 0; i < items; ++i)
    doc.paragraphs.push_back(numbered("Item", 1, 1, size));
  writeit::Paragraph end;
  writeit::Run run;
  run.text = "End";
  end.runs.push_back(run);
  doc.paragraphs.push_back(end);
  Glib::file_set_contents(path, writeit::rtf_export(doc));
  MainWindowProbe::open(w, path);
  settle();
}

// Renumbers past the edit, not only at the end of the list: Enter in the
// middle of 12 items moves item 9 to 10, and 99 to 100 items by Enter goes
// back by undo and by deleting an item whole. The size puts the two labels
// at different places whatever Sans resolves to.
void hundred(const std::string& home)
{
  const std::string path = Glib::build_filename(home, "hundred.rtf");
  writeit::MainWindow window;
  window.show();
  settle();

  int size = boundary_size(window, "9.", "10.");
  std::cout << "  9./10. differ at Sans " << size << "\n";
  CHECK(size > 0);
  open_list(window, path, 12, size);
  CHECK(all_placed(window, 12, size));
  MainWindowProbe::enter_at_end(window, 3);
  MainWindowProbe::type_at_end(window, 4, "New");
  settle();
  CHECK(all_placed(window, 13, size));
  MainWindowProbe::undo(window);
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, 12, size));

  size = boundary_size(window, "99.", "100.");
  std::cout << "  99./100. differ at Sans " << size << "\n";
  CHECK(size > 0);
  open_list(window, path, 99, size);
  CHECK(all_placed(window, 99, size));
  MainWindowProbe::enter_at_end(window, 49);
  MainWindowProbe::type_at_end(window, 50, "New");
  settle();
  CHECK(all_placed(window, 100, size));
  MainWindowProbe::undo(window);
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, 99, size));
  MainWindowProbe::enter_at_end(window, 98);
  MainWindowProbe::type_at_end(window, 99, "New");
  settle();
  CHECK(all_placed(window, 100, size));
  MainWindowProbe::delete_items(window, 9, 9);
  settle();
  CHECK(all_placed(window, 99, size));
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
  // After the list, another list and plain paragraphs, so a renumber of the
  // first list is told apart from a look at the whole document.
  constexpr int kOthers = 1000;
  constexpr int kParagraphs = kItems + kOthers;
  writeit::Document doc;
  for (int i = 0; i < kItems; ++i)
    doc.paragraphs.push_back(numbered("Item", 1, 1));
  for (int i = 0; i < kOthers / 2; ++i)
    doc.paragraphs.push_back(numbered("Other", 2, 1));
  for (int i = 0; i < kOthers / 2; ++i) {
    writeit::Paragraph plain;
    writeit::Run run;
    run.text = "Plain";
    plain.runs.push_back(run);
    doc.paragraphs.push_back(plain);
  }
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
  // to their new numbers' places. Their labels change, some across a digit
  // boundary, so that list's items may be looked at again, but not the
  // other list or the plain paragraphs below it.
  long before = MainWindowProbe::evaluated(window);
  MainWindowProbe::enter_at_end(window, 8);
  settle();
  long looked = MainWindowProbe::evaluated(window) - before;
  std::cout << "  Enter after item 9: " << looked << " of " << kParagraphs + 1 << " paragraphs\n";
  CHECK(looked <= kItems + 3 && looked < kParagraphs);
  CHECK(all_placed(window, kItems + 1));
  // Undo puts the whole document back, so looks at all of it again.
  MainWindowProbe::undo(window);
  settle();
  CHECK(all_placed(window, kItems));
  // Deleting an item whole renumbers as Enter does, in place.
  before = MainWindowProbe::evaluated(window);
  MainWindowProbe::delete_items(window, 8, 8);
  settle();
  looked = MainWindowProbe::evaluated(window) - before;
  std::cout << "  deleting item 9: " << looked << " of " << kParagraphs - 1 << " paragraphs\n";
  CHECK(looked <= kItems + 3 && looked < kParagraphs - 1);
  CHECK(all_placed(window, kItems - 1));
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

constexpr int kChecks = 74;

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
  hundred(home);
  long_list(home);

  g_remove(path.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("list-tabs", kChecks);
}
