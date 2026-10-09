/* SPDX-License-Identifier: Unlicense */

// The status bar of the real window, under a display (CI runs the suite in
// xvfb-run): the message, then "Page n of m", then the zoom cell, which
// pops the same list as View > Zoom.

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
  static std::string page_text(MainWindow& w)
  {
    return w.page_label_.get_text();
  }
  static std::string zoom_text(MainWindow& w)
  {
    return w.zoom_label_.get_text();
  }
  static void caret_to(MainWindow& w, bool end)
  {
    w.buffer_->place_cursor(end ? w.buffer_->end() : w.buffer_->begin());
  }
  static void view(MainWindow& w, ViewMode mode)
  {
    (mode == ViewMode::Page ? w.page_item_ : w.draft_item_)->set_active(true);
  }
  static Gtk::RadioMenuItem* view_zoom(MainWindow& w, int i)
  {
    return w.zoom_view_[i];
  }
  static Gtk::RadioMenuItem* cell_zoom(MainWindow& w, int i)
  {
    return w.zoom_status_[i];
  }
  static Gtk::Menu& popup(MainWindow& w)
  {
    return w.zoom_popup_;
  }
  static Gtk::Widget& cell(MainWindow& w)
  {
    return w.zoom_cell_;
  }
  static Gtk::Widget& page_cell(MainWindow& w)
  {
    return w.page_label_;
  }
  static Gtk::Widget& message(MainWindow& w)
  {
    return w.message_;
  }
  static Gtk::Widget& status(MainWindow& w)
  {
    return w.status_;
  }
  // Narrow enough for any test screen: no toolbars, 800 px.
  static void narrow(MainWindow& w)
  {
    w.standard_bar_.hide();
    w.format_bar_.hide();
    w.resize(800, 600);
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;
using writeit::ViewMode;

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

std::string letter(int paragraphs)
{
  std::string rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0\\fs22 ";
  for (int i = 0; i < paragraphs; ++i)
    rtf += "Paragraph " + std::to_string(i + 1) + " of the letter.\\par ";
  return rtf + "}";
}

// "Page n of m" -> m, or -1.
int pages_in(const std::string& label)
{
  const auto of = label.find(" of ");
  if (label.rfind("Page ", 0) != 0 || of == std::string::npos)
    return -1;
  return std::atoi(label.c_str() + of + 4);
}

void cells(writeit::MainWindow& window)
{
  // Message, page, zoom, left to right, all three shown.
  auto& status = MainWindowProbe::status(window);
  int mx = 0, px = 0, zx = 0, y = 0;
  MainWindowProbe::message(window).translate_coordinates(status, 0, 0, mx, y);
  MainWindowProbe::page_cell(window).translate_coordinates(status, 0, 0, px, y);
  MainWindowProbe::cell(window).translate_coordinates(status, 0, 0, zx, y);
  CHECK(MainWindowProbe::page_cell(window).is_visible());
  CHECK(MainWindowProbe::cell(window).is_visible());
  CHECK(mx < px);
  CHECK(px < zx);
  // A first launch (no ini) is at Fit width, and the cell says so.
  CHECK(MainWindowProbe::zoom_text(window) == "Fit width");
}

void short_letter(writeit::MainWindow& window, const std::string& dir)
{
  const std::string path = Glib::build_filename(dir, "short.rtf");
  Glib::file_set_contents(path, letter(2));
  MainWindowProbe::open(window, path);
  settle();
  CHECK(MainWindowProbe::page_text(window) == "Page 1 of 1");
  g_remove(path.c_str());
}

void long_letter(writeit::MainWindow& window, const std::string& dir)
{
  // 150 lines of 11 pt Sans is several A4 pages.
  const std::string path = Glib::build_filename(dir, "long.rtf");
  Glib::file_set_contents(path, letter(150));
  MainWindowProbe::open(window, path);
  MainWindowProbe::caret_to(window, false);
  settle();
  const std::string first = MainWindowProbe::page_text(window);
  const int pages = pages_in(first);
  if (pages < 3)
    std::cerr << "long letter: " << first << "\n";
  CHECK(pages >= 3);
  CHECK(first == "Page 1 of " + std::to_string(pages));
  // The caret's page follows the caret.
  MainWindowProbe::caret_to(window, true);
  settle();
  const std::string last = MainWindowProbe::page_text(window);
  if (last != writeit::page_label({pages, pages}))
    std::cerr << "caret at end: " << last << "\n";
  CHECK(last == writeit::page_label({pages, pages}));

  // The count is the printed one: the same in Draft and at any zoom, give or
  // take one page for the rounding of line heights at a zoom.
  for (ViewMode mode : {ViewMode::Draft, ViewMode::Page}) {
    MainWindowProbe::view(window, mode);
    for (int i = 0; i < 5; ++i) {
      MainWindowProbe::view_zoom(window, i)->activate();
      settle();
      const int at = pages_in(MainWindowProbe::page_text(window));
      if (std::abs(at - pages) > 1)
        std::cerr << "zoom " << i << ": " << MainWindowProbe::page_text(window) << "\n";
      CHECK(std::abs(at - pages) <= 1);
    }
  }
  MainWindowProbe::view_zoom(window, 2)->activate();
  settle();
  g_remove(path.c_str());
}

void zoom_cell(writeit::MainWindow& window)
{
  // The cell pops the same list as View > Zoom, every item shown.
  auto& popup = MainWindowProbe::popup(window);
  popup.popup_at_widget(&MainWindowProbe::cell(window), Gdk::GRAVITY_NORTH_EAST,
                        Gdk::GRAVITY_SOUTH_EAST, nullptr);
  settle();
  CHECK(popup.is_visible());
  // It pops at the cell: its bottom-right corner on the cell's top-right.
  {
    auto& cell = MainWindowProbe::cell(window);
    int cell_x = 0, cell_y = 0, win_x = 0, win_y = 0, menu_x = 0, menu_y = 0;
    cell.translate_coordinates(window, 0, 0, cell_x, cell_y);
    window.get_window()->get_origin(win_x, win_y);
    auto* menu_window = popup.get_toplevel();
    menu_window->get_window()->get_origin(menu_x, menu_y);
    const int cell_right = win_x + cell_x + cell.get_allocated_width();
    const int cell_top = win_y + cell_y;
    const int menu_right = menu_x + menu_window->get_allocated_width();
    const int menu_bottom = menu_y + menu_window->get_allocated_height();
    if (std::abs(menu_right - cell_right) > 2 || std::abs(menu_bottom - cell_top) > 2)
      std::cerr << "zoom popup at " << menu_x << "," << menu_y << ", bottom-right " << menu_right
                << "," << menu_bottom << "; cell top-right " << cell_right << "," << cell_top
                << "\n";
    CHECK(std::abs(menu_right - cell_right) <= 2);
    CHECK(std::abs(menu_bottom - cell_top) <= 2);
  }
  for (int i = 0; i < 6; ++i) {
    auto* view_item = MainWindowProbe::view_zoom(window, i);
    auto* cell_item = MainWindowProbe::cell_zoom(window, i);
    CHECK(cell_item->is_visible());
    CHECK(cell_item->get_label() == view_item->get_label());
  }
  CHECK(popup.get_allocated_height() > 6 * 10);
  // Choosing from the cell is choosing from View, and the cell says so.
  MainWindowProbe::cell_zoom(window, 3)->activate();
  popup.popdown();
  settle();
  CHECK(MainWindowProbe::zoom_text(window) == "150%");
  CHECK(MainWindowProbe::view_zoom(window, 3)->get_active());
  MainWindowProbe::view_zoom(window, 2)->activate();
  settle();
  CHECK(MainWindowProbe::zoom_text(window) == "100%");
  CHECK(MainWindowProbe::cell_zoom(window, 2)->get_active());
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 39;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-status-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "status-bar: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    cells(window);
    short_letter(window, home);
    long_letter(window, home);
    MainWindowProbe::narrow(window);
    settle();
    zoom_cell(window);
    window.hide();
    settle();
  }
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("status-bar", kChecks);
}
