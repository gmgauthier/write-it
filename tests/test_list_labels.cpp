/* SPDX-License-Identifier: Unlicense */

// The real window, under a display (CI runs the suite in xvfb-run): where a
// list item's label lands beside its text. A centred item centres its label
// and text together, as one unit, between the first-line indent and the
// right indent, as Word 97 does; a right-aligned one ends at the right
// indent with the label in front; a left-aligned one hangs the label at the
// first-line indent. Measured with a non-standard 0.75" hang, at two zooms.

#include "check.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
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
  struct Item {
    int label_x = 0;
    int label_width = 0;
    int text_start = 0;
    int text_end = 0;
    // A space in the label's font: the least room between label and text.
    int gap = 0;
  };
  // The label and the first display line's text of paragraph `line`.
  static Item item(MainWindow& w, int line)
  {
    const Document doc = w.capture();
    const std::vector<int> numbers = list_numbers(doc.paragraphs);
    auto start = w.buffer_->get_iter_at_line(line);
    Item out;
    Glib::RefPtr<Pango::Layout> layout;
    Gdk::Rectangle where;
    // A plain paragraph has no label: only its text is measured.
    if (w.list_label_place(doc.paragraphs[static_cast<size_t>(line)], start.get_offset(),
                           numbers[static_cast<size_t>(line)], layout, out.label_x, where)) {
      int height = 0;
      layout->get_pixel_size(out.label_width, height);
      auto space = w.text_.create_pango_layout(" ");
      space->set_font_description(layout->get_font_description());
      space->get_pixel_size(out.gap, height);
    } else {
      w.text_.get_iter_location(start, where);
    }
    out.text_start = where.get_x();
    auto end = start;
    w.text_.forward_display_line_end(end);
    Gdk::Rectangle last;
    w.text_.get_iter_location(end, last);
    out.text_end = last.get_x();
    return out;
  }
  // Paragraph `line`'s second display line, start and end x; false when the
  // paragraph does not wrap.
  static bool wrapped(MainWindow& w, int line, int& start_x, int& end_x)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    if (!w.text_.forward_display_line(iter) || iter.get_line() != line)
      return false;
    Gdk::Rectangle where;
    w.text_.get_iter_location(iter, where);
    start_x = where.get_x();
    w.text_.forward_display_line_end(iter);
    w.text_.get_iter_location(iter, where);
    end_x = where.get_x();
    return true;
  }
  // The left margin GTK centres paragraph `line` from: its screen-only shift
  // tag's, or -1 when it has none. And how many shift tags exist.
  static int shift(MainWindow& w, int line)
  {
    for (const auto& tag : w.buffer_->get_iter_at_line(line).get_tags()) {
      const std::string name = tag->property_name().get_value();
      if (name.rfind("list-shift", 0) == 0)
        return tag->property_left_margin().get_value();
    }
    return -1;
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
  static void type_at_end(MainWindow& w, int line, const Glib::ustring& text)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    iter.forward_to_line_end();
    w.buffer_->place_cursor(iter);
    w.buffer_->begin_user_action();
    w.buffer_->insert_interactive_at_cursor(text, true);
    w.buffer_->end_user_action();
  }
  static void align(MainWindow& w, int line, Align align)
  {
    w.buffer_->place_cursor(w.buffer_->get_iter_at_line(line));
    w.apply_align(align);
  }
  // One full shift pass over the document, in microseconds.
  static gint64 time_shifts(MainWindow& w)
  {
    const gint64 start = g_get_monotonic_time();
    w.update_list_shifts();
    return g_get_monotonic_time() - start;
  }
  static gint64 time_capture(MainWindow& w)
  {
    const gint64 start = g_get_monotonic_time();
    w.capture();
    return g_get_monotonic_time() - start;
  }
  static int shift_tags(MainWindow& w)
  {
    int count = 0;
    w.buffer_->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
      if (tag->property_name().get_value().rfind("list-shift", 0) == 0)
        ++count;
    });
    return count;
  }
  // Buffer x of twips from the page's left edge, as the tags place them.
  static int x_of(MainWindow& w, int twips)
  {
    return w.margin_left() + w.indent_px(twips);
  }
  static int px(MainWindow& w, int twips)
  {
    return w.indent_px(twips);
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

void settle()
{
  for (int i = 0; i < 60; ++i) {
    while (Gtk::Main::events_pending())
      Gtk::Main::iteration(false);
    g_usleep(10000);
  }
}

// Numbered items, one list: with a 0.75" hang (\li1440\fi-1080) centred,
// right and left; a standard (\li720\fi-360) centred item; a centred item
// with a very narrow hang (\fi-60); a centred 36-point item whose label is
// wider than its hang in any font; a wrapped centred item with the standard
// hang and one with the narrow hang; and a plain right-aligned line whose
// end is the right indent.
const char* kItems =
    "{\\rtf1\\ansi{\\fonttbl{\\f0\\fswiss Sans;}}"
    "{\\*\\listtable{\\list{\\listlevel\\levelnfc0}\\listid1}}"
    "{\\*\\listoverridetable{\\listoverride\\listid1\\ls1}}"
    "\\pard\\li1440\\fi-1080\\qc\\ls1\\f0\\fs22 Centred item\\par"
    "\\pard\\li1440\\fi-1080\\qr\\ls1\\f0\\fs22 Right item\\par"
    "\\pard\\li1440\\fi-1080\\ls1\\f0\\fs22 Left item\\par"
    "\\pard\\li720\\fi-360\\qc\\ls1\\f0\\fs22 Standard centred item\\par"
    "\\pard\\li720\\fi-60\\qc\\ls1\\f0\\fs22 Narrow centred item\\par"
    "\\pard\\li720\\fi-360\\qc\\ls1\\f0\\fs72 Big\\par"
    "\\pard\\li720\\fi-360\\qc\\ls1\\f0\\fs22 Wrapped centred item whose text runs on well past the end of one line, so that it wraps onto a second line and a third at every zoom the test uses, which shows where its wrapped lines sit against the right indent and the left indent of the paragraph.\\par"
    "\\pard\\li720\\fi-60\\qc\\ls1\\f0\\fs22 Wrapped centred item whose text runs on well past the end of one line, so that it wraps onto a second line and a third at every zoom the test uses, which shows where its wrapped lines sit against the right indent and the left indent of the paragraph.\\par"
    "\\pard\\qr\\f0\\fs22 edge\\par}";

bool near(int a, int b, int slack)
{
  return a - b <= slack && b - a <= slack;
}

// The room before a list item's text: its own room (twips, from
// list_label_space()), or the label and a space when that is wider.
int label_room(writeit::MainWindow& window, const MainWindowProbe::Item& item, int room)
{
  return std::max(MainWindowProbe::px(window, room), item.label_width + item.gap);
}

// A one-line centred item, or a wrapped one's first line: the unit, label
// start to text end, centred between the first-line indent (`first_twips`)
// and the right indent, as Word 97 does, to within a pixel of rounding; the
// label `room` twips (or its width and a space) before the text.
bool unit_centred(writeit::MainWindow& window, const MainWindowProbe::Item& item, int first_twips,
                  int room, int right)
{
  const int column_left = MainWindowProbe::x_of(window, first_twips);
  return near(item.label_x + item.text_end, column_left + right, 2) &&
         near(item.text_start - item.label_x, label_room(window, item, room), 1);
}

// A wrapped centred item's second line: centred between the left indent and
// the right indent in Word; here offset right by half of d, where d is
// (first-line indent + label room) - left indent, the spec's rule. `d_px`
// gets d and `off2` twice the measured offset.
bool wrap_offset(writeit::MainWindow& window, int line, const MainWindowProbe::Item& first,
                 int left_twips, int first_twips, int room, int right, int& d_px, int& off2)
{
  int start_x = 0;
  int end_x = 0;
  if (!MainWindowProbe::wrapped(window, line, start_x, end_x))
    return false;
  const int left_x = MainWindowProbe::x_of(window, left_twips);
  d_px = MainWindowProbe::x_of(window, first_twips) + label_room(window, first, room) - left_x;
  off2 = (start_x + end_x) - (left_x + right);
  const int shift = MainWindowProbe::shift(window, line);
  const bool tag_ok = d_px == 0 ? shift < 0 : near(shift, left_x + d_px, 1);
  return tag_ok && near(off2, d_px, 2);
}

void expect(writeit::MainWindow& window, int percent)
{
  const int right = MainWindowProbe::item(window, 8).text_end;
  const auto centred = MainWindowProbe::item(window, 0);
  const auto right_item = MainWindowProbe::item(window, 1);
  const auto left_item = MainWindowProbe::item(window, 2);
  const auto standard = MainWindowProbe::item(window, 3);
  const auto narrow = MainWindowProbe::item(window, 4);
  const auto big = MainWindowProbe::item(window, 5);
  const auto wrap_std = MainWindowProbe::item(window, 6);
  const auto wrap_narrow = MainWindowProbe::item(window, 7);
  // One-line centred items match Word exactly: 0.75" hang, standard hang,
  // a hang narrower than the quarter-inch room (\fi-60: first-line indent
  // 660, room 360), and a label wider than its hang.
  const bool ok_centred = unit_centred(window, centred, 360, 1080, right);
  const bool ok_standard = unit_centred(window, standard, 360, 360, right);
  const bool ok_narrow = unit_centred(window, narrow, 660, 360, right);
  const bool ok_big = big.label_width + big.gap > MainWindowProbe::px(window, 360) &&
                      unit_centred(window, big, 360, 360, right);
  // Right-aligned: the text ends at the right indent, the label its room
  // before it. Left-aligned: the label at the first-line indent, the text
  // at the left indent. Neither is shifted.
  const bool ok_right = near(right_item.text_end, right, 1) &&
                        near(right_item.text_start - right_item.label_x,
                             label_room(window, right_item, 1080), 1) &&
                        MainWindowProbe::shift(window, 1) < 0;
  const bool ok_left = left_item.label_x == MainWindowProbe::x_of(window, 360) &&
                       near(left_item.text_start, MainWindowProbe::x_of(window, 1440), 1) &&
                       MainWindowProbe::shift(window, 2) < 0;
  // Wrapped: the first line still matches Word; later lines sit d/2 right of
  // Word's. Standard hang: d is how far the label and a space reach past the
  // quarter inch, 0 when they fit. Narrow hang: d is at least the 0.25" room
  // less the hang.
  const bool ok_wrap_std_first = unit_centred(window, wrap_std, 360, 360, right);
  int d_std = 0;
  int off_std = 0;
  const bool ok_wrap_std =
      wrap_offset(window, 6, wrap_std, 720, 360, 360, right, d_std, off_std) &&
      near(d_std,
           std::max(0, wrap_std.label_width + wrap_std.gap - MainWindowProbe::px(window, 360)), 1);
  const bool ok_wrap_narrow_first = unit_centred(window, wrap_narrow, 660, 360, right);
  int d_narrow = 0;
  int off_narrow = 0;
  const bool ok_wrap_narrow =
      wrap_offset(window, 7, wrap_narrow, 720, 660, 360, right, d_narrow, off_narrow) &&
      d_narrow >= MainWindowProbe::px(window, 300) - 1;
  // One shift tag per distinct margin in use, no more.
  const int tags = MainWindowProbe::shift_tags(window);
  const bool ok_tags = tags >= 1 && tags <= 4;
  std::cout << "zoom " << percent << ": standard label " << standard.label_x << " w "
            << standard.label_width << "+" << standard.gap << " text " << standard.text_start
            << ".." << standard.text_end << "; narrow label " << narrow.label_x << " text "
            << narrow.text_start << ".." << narrow.text_end << "; big label " << big.label_x
            << " w " << big.label_width << "+" << big.gap << " text " << big.text_start << ".."
            << big.text_end << "; right edge " << right << "; wrapped d " << d_std << " off "
            << off_std << "/2, narrow d " << d_narrow << " off " << off_narrow << "/2; tags "
            << tags << "\n";
  CHECK(ok_centred);
  CHECK(ok_standard);
  CHECK(ok_narrow);
  CHECK(ok_big);
  CHECK(ok_right);
  CHECK(ok_left);
  CHECK(ok_wrap_std_first);
  CHECK(ok_wrap_std);
  CHECK(ok_wrap_narrow_first);
  CHECK(ok_wrap_narrow);
  CHECK(ok_tags);
}

// The shift is screen-only: the document, its RTF, the dirty check, and
// undo never see it, and editing, undo, and alignment keep it right.
void screen_only(writeit::MainWindow& window, const std::string& file_text)
{
  using writeit::Align;
  writeit::Document read;
  writeit::rtf_import(file_text, read);
  const writeit::Document shown = MainWindowProbe::doc(window);
  CHECK(MainWindowProbe::shift_tags(window) >= 1 && !MainWindowProbe::dirty(window));
  CHECK(shown == read);
  CHECK(writeit::rtf_export(shown) == writeit::rtf_export(read));
  const int right = MainWindowProbe::item(window, 8).text_end;
  // Typing in the narrow-hang item keeps it centred as Word does.
  MainWindowProbe::type_at_end(window, 4, " grows");
  settle();
  CHECK(MainWindowProbe::dirty(window));
  CHECK(unit_centred(window, MainWindowProbe::item(window, 4), 660, 360, right));
  // Undo gives back the very document, clean, still centred.
  MainWindowProbe::undo(window);
  settle();
  CHECK(MainWindowProbe::doc(window) == read && !MainWindowProbe::dirty(window));
  CHECK(unit_centred(window, MainWindowProbe::item(window, 4), 660, 360, right));
  // Aligning the 36-point item left drops its shift; undo brings it back.
  MainWindowProbe::align(window, 5, Align::Left);
  settle();
  CHECK(MainWindowProbe::shift(window, 5) < 0 &&
        MainWindowProbe::item(window, 5).label_x == MainWindowProbe::x_of(window, 360));
  MainWindowProbe::undo(window);
  settle();
  CHECK(MainWindowProbe::shift(window, 5) >= 0 &&
        unit_centred(window, MainWindowProbe::item(window, 5), 360, 360, right));
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 33;

int main(int argc, char* argv[])
{
  // A private config folder, so the test never touches the user's ini.
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-list-labels-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "list-labels: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  const std::string file = Glib::build_filename(home, "items.rtf");
  Glib::file_set_contents(file, kItems);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, file);
    settle();
    for (int percent : {100, 200}) {
      MainWindowProbe::zoom(window, percent);
      settle();
      expect(window, percent);
    }
    window.hide();
    settle();
  }
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, file);
    settle();
    screen_only(window, kItems);
    window.hide();
    settle();
  }
  // A long list: 2000 centred items, every label wider than its hang. The
  // pass that runs after each change stays quick, and the table holds one
  // tag per margin in use.
  const std::string long_file = Glib::build_filename(home, "long.rtf");
  {
    std::string rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0\\fswiss Sans;}}"
                      "{\\*\\listtable{\\list{\\listlevel\\levelnfc0}\\listid1}}"
                      "{\\*\\listoverridetable{\\listoverride\\listid1\\ls1}}";
    for (int i = 0; i < 2000; ++i)
      rtf += "\\pard\\li720\\fi-360\\qc\\ls1\\f0\\fs48 Item\\par";
    rtf += "}";
    Glib::file_set_contents(long_file, rtf);
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, long_file);
    settle();
    const gint64 us = MainWindowProbe::time_shifts(window);
    const int tags = MainWindowProbe::shift_tags(window);
    std::cout << "capture " << MainWindowProbe::time_capture(window) / 1000 << " ms\n";
    std::cout << "2000 centred items: shift pass " << us / 1000 << " ms, " << tags << " tags\n";
    CHECK(tags >= 1 && tags <= 8);
    CHECK(us < 3000000);
    window.hide();
    settle();
  }

  g_remove(long_file.c_str());
  g_remove(file.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("list-labels", kChecks);
}
