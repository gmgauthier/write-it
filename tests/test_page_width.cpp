/* SPDX-License-Identifier: Unlicense */

// The real window, under a display (CI runs the suite in xvfb-run). It opens
// a short letter with an indented, a centred and a right-aligned paragraph,
// walks View > Zoom and View > Page / Draft, and checks what GTK actually
// allocated and where it actually wrapped: the page's width, and the right
// edge of the right-aligned line, which sits on the wrap width.

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
  static void view(MainWindow& w, ViewMode mode)
  {
    (mode == ViewMode::Page ? w.page_item_ : w.draft_item_)->set_active(true);
  }
  static int page_width(MainWindow& w)
  {
    return w.page_.get_allocated_width();
  }
  static int text_width(MainWindow& w)
  {
    return w.text_.get_allocated_width();
  }
  // Buffer x of the end of a paragraph's last line, and of its start.
  static int line_end_x(MainWindow& w, int line)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    iter.forward_to_line_end();
    Gdk::Rectangle rect;
    w.text_.get_iter_location(iter, rect);
    return rect.get_x();
  }
  static int line_start_x(MainWindow& w, int line)
  {
    Gdk::Rectangle rect;
    w.text_.get_iter_location(w.buffer_->get_iter_at_line(line), rect);
    return rect.get_x();
  }
  static int left_margin(MainWindow& w)
  {
    return w.text_.get_left_margin();
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;
using writeit::ViewMode;

const char kLetter[] =
    "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0\\fs22 Dear Mrs Hart,\\par\\li720 This paragraph "
    "is indented half an inch on the left so the Draft view can show that indents follow the "
    "zoom level properly.\\par\\pard\\qc A centred line.\\par\\pard\\qr A right-aligned "
    "line.\\par}";
constexpr int kCentred = 2;
constexpr int kRight = 3;

void settle()
{
  auto context = Glib::MainContext::get_default();
  // Layout runs in idles and in the frame clock; give both a few rounds.
  for (int round = 0; round < 6; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

const char* name(ViewMode mode)
{
  return mode == ViewMode::Page ? "Page" : "Draft";
}

void expect(writeit::MainWindow& window, ViewMode mode, int percent)
{
  const auto want = writeit::page_widths(mode, percent / 100.0);
  const int page = MainWindowProbe::page_width(window);
  const int text = MainWindowProbe::text_width(window);
  const int left = MainWindowProbe::left_margin(window);
  const int right_end = MainWindowProbe::line_end_x(window, kRight);
  const int centre = (MainWindowProbe::line_start_x(window, kCentred) +
                      MainWindowProbe::line_end_x(window, kCentred)) /
                     2;
  const bool ok_page = page == want.page;
  const bool ok_text = text <= want.page;
  // The right-aligned line ends on the wrap; the centred one sits on its middle.
  const bool ok_right = std::abs(right_end - (left + want.wrap)) <= 2;
  const bool ok_centre = std::abs(centre - (left + want.wrap / 2)) <= 3;
  if (!(ok_page && ok_text && ok_right && ok_centre))
    std::cerr << name(mode) << " " << percent << "%: page " << page << " (want " << want.page
              << "), text view " << text << ", right edge " << right_end << " (want "
              << left + want.wrap << "), centre " << centre << " (want " << left + want.wrap / 2
              << ")\n";
  CHECK(ok_page);
  CHECK(ok_text);
  CHECK(ok_right);
  CHECK(ok_centre);
}

void go(writeit::MainWindow& window, ViewMode mode, int percent)
{
  MainWindowProbe::view(window, mode);
  MainWindowProbe::zoom(window, percent);
  settle();
  expect(window, mode, percent);
}

}  // namespace

int main(int argc, char* argv[])
{
  // A private config folder, so the test never touches the user's ini.
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-page-width-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "page-width: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  const std::string letter = Glib::build_filename(home, "draftcheck.rtf");
  Glib::file_set_contents(letter, kLetter);

  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, letter);
    settle();
    expect(window, ViewMode::Page, 100);

    // Each view at each menu zoom, from 100% down, then up.
    for (ViewMode mode : {ViewMode::Page, ViewMode::Draft})
      for (int percent : {50, 75, 100, 150, 200, 150, 100, 75, 50})
        go(window, mode, percent);

    // SysAdmin's sequences: Draft 200 then Page 100, and on to Draft 50.
    go(window, ViewMode::Draft, 200);
    go(window, ViewMode::Page, 100);
    go(window, ViewMode::Draft, 50);
    go(window, ViewMode::Page, 200);
    go(window, ViewMode::Draft, 50);
    go(window, ViewMode::Page, 50);
    go(window, ViewMode::Draft, 100);

    // Every ordered pair of states.
    const int zooms[] = {50, 75, 100, 150, 200};
    for (ViewMode a : {ViewMode::Page, ViewMode::Draft})
      for (int za : zooms)
        for (ViewMode b : {ViewMode::Page, ViewMode::Draft})
          for (int zb : zooms) {
            go(window, a, za);
            go(window, b, zb);
          }
    window.hide();
    settle();
  }

  g_remove(letter.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("page-width");
}
