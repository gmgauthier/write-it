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

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
  };
  // The label and the first line's text of paragraph `line`.
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
    } else {
      w.text_.get_iter_location(start, where);
    }
    out.text_start = where.get_x();
    auto end = start;
    end.forward_to_line_end();
    Gdk::Rectangle last;
    w.text_.get_iter_location(end, last);
    out.text_end = last.get_x();
    return out;
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
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  // Restart (or continue) numbering at paragraph `line`, as the menu does.
  static bool renumber(MainWindow& w, int line, bool restart)
  {
    w.buffer_->place_cursor(w.buffer_->get_iter_at_line(line));
    return w.renumber_list(restart);
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

// Numbered items with a 0.75" hang (\li1440\fi-1080), centred, right and
// left; a standard centred item; and a plain right-aligned line whose end
// is the right indent.
const char* kItems =
    "{\\rtf1\\ansi{\\fonttbl{\\f0\\fswiss Sans;}}"
    "{\\*\\listtable{\\list{\\listlevel\\levelnfc0}\\listid1}}"
    "{\\*\\listoverridetable{\\listoverride\\listid1\\ls1}}"
    "\\pard\\li1440\\fi-1080\\qc\\ls1\\f0\\fs22 Centred item\\par"
    "\\pard\\li1440\\fi-1080\\qr\\ls1\\f0\\fs22 Right item\\par"
    "\\pard\\li1440\\fi-1080\\ls1\\f0\\fs22 Left item\\par"
    "\\pard\\li720\\fi-360\\qc\\ls1\\f0\\fs22 Standard centred item\\par"
    "\\pard\\qr\\f0\\fs22 edge\\par}";

bool near(int a, int b, int slack)
{
  return a - b <= slack && b - a <= slack;
}

void expect(writeit::MainWindow& window, int percent)
{
  const int right = MainWindowProbe::item(window, 4).text_end;
  // Centred, 0.75" hang: the unit, label start to text end, is centred
  // between the first-line indent (left + first = 360) and the right indent.
  const auto centred = MainWindowProbe::item(window, 0);
  const int column_left = MainWindowProbe::x_of(window, 360);
  const int unit_mid = (centred.label_x + centred.text_end) / 2;
  const int column_mid = (column_left + right) / 2;
  const bool ok_centred = near(unit_mid, column_mid, 2);
  // Its label sits the paragraph's own hang before the text.
  const bool ok_centred_hang =
      near(centred.text_start - centred.label_x, MainWindowProbe::px(window, 1080), 1);
  // Right-aligned: the text ends at the right indent, the label a hang
  // before it.
  const auto right_item = MainWindowProbe::item(window, 1);
  const bool ok_right =
      near(right_item.text_end, right, 1) &&
      near(right_item.text_start - right_item.label_x, MainWindowProbe::px(window, 1080), 1);
  // Left-aligned: the label at the first-line indent, the text at the left
  // indent.
  const auto left_item = MainWindowProbe::item(window, 2);
  const bool ok_left = left_item.label_x == column_left &&
                       near(left_item.text_start, MainWindowProbe::x_of(window, 1440), 1);
  // The standard hang (\li720\fi-360) centres the same way.
  const auto standard = MainWindowProbe::item(window, 3);
  const bool ok_standard =
      near((standard.label_x + standard.text_end) / 2, column_mid, 2) &&
      near(standard.text_start - standard.label_x, MainWindowProbe::px(window, 360), 1);
  if (!(ok_centred && ok_centred_hang && ok_right && ok_left && ok_standard))
    std::cout << "zoom " << percent << ": centred label " << centred.label_x << " text "
              << centred.text_start << ".." << centred.text_end << " unit mid " << unit_mid
              << " column " << column_left << ".." << right << " mid " << column_mid
              << "; right label " << right_item.label_x << " text " << right_item.text_start << ".."
              << right_item.text_end << "; left label " << left_item.label_x << " text "
              << left_item.text_start << "; standard label " << standard.label_x << " text "
              << standard.text_start << ".." << standard.text_end << "\n";
  CHECK(ok_centred);
  CHECK(ok_centred_hang);
  CHECK(ok_right);
  CHECK(ok_left);
  CHECK(ok_standard);
}

// Three numbered items in a named style. Restart Numbering and Continue
// Previous List change which list an item is in, never its style.
const char* kStyled =
    "{\\rtf1\\ansi{\\fonttbl{\\f0\\fswiss Sans;}}"
    "{\\stylesheet{\\s0\\f0\\fs22 Normal;}{\\s1\\sbasedon0\\f0\\fs22 Steps;}}"
    "{\\*\\listtable{\\list{\\listlevel\\levelnfc0}\\listid1}}"
    "{\\*\\listoverridetable{\\listoverride\\listid1\\ls1}}"
    "\\pard\\s1\\li720\\fi-360\\ls1\\f0\\fs22 One\\par"
    "\\pard\\s1\\li720\\fi-360\\ls1\\f0\\fs22 Two\\par"
    "\\pard\\s1\\li720\\fi-360\\ls1\\f0\\fs22 Three\\par}";

bool all_steps(const writeit::Document& doc)
{
  for (const auto& paragraph : doc.paragraphs)
    if (paragraph.style != "Steps")
      return false;
  return true;
}

void renumbering(writeit::MainWindow& window)
{
  CHECK(all_steps(MainWindowProbe::doc(window)));
  CHECK(MainWindowProbe::renumber(window, 1, true));
  settle();
  const writeit::Document restarted = MainWindowProbe::doc(window);
  CHECK(writeit::list_numbers(restarted.paragraphs) == std::vector<int>({1, 1, 2}));
  CHECK(all_steps(restarted));
  CHECK(MainWindowProbe::renumber(window, 1, false));
  settle();
  const writeit::Document joined = MainWindowProbe::doc(window);
  CHECK(writeit::list_numbers(joined.paragraphs) == std::vector<int>({1, 2, 3}));
  CHECK(all_steps(joined));
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 17;

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
  const std::string styled = Glib::build_filename(home, "styled.rtf");
  Glib::file_set_contents(styled, kStyled);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, styled);
    settle();
    renumbering(window);
    window.hide();
    settle();
  }

  g_remove(styled.c_str());
  g_remove(file.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("list-labels", kChecks);
}
