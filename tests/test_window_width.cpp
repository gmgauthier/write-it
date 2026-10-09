/* SPDX-License-Identifier: Unlicense */

// The real window under a display (CI runs the suite in xvfb-run). It must
// fit a 1024 px screen: the narrowest it can be made is at most 1024 px,
// with the toolbars on one row (the default) and stacked.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static bool side_by_side(MainWindow& w)
  {
    return w.settings_.toolbars_side_by_side;
  }
  static void set_side_by_side(MainWindow& w, bool on)
  {
    w.settings_.toolbars_side_by_side = on;
    w.apply_toolbar_row();
  }
  static int format_bar_x(MainWindow& w)
  {
    int x = 0;
    int y = 0;
    w.format_bar_.translate_coordinates(w, 0, 0, x, y);
    return x;
  }
  static int format_bar_y(MainWindow& w)
  {
    int x = 0;
    int y = 0;
    w.format_bar_.translate_coordinates(w, 0, 0, x, y);
    return y;
  }
  static int standard_bar_y(MainWindow& w)
  {
    int x = 0;
    int y = 0;
    w.standard_bar_.translate_coordinates(w, 0, 0, x, y);
    return y;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

constexpr int kScreen = 1024;

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

int minimum_width(writeit::MainWindow& window)
{
  int minimum = 0;
  int natural = 0;
  window.get_preferred_width(minimum, natural);
  return minimum;
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 9;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-window-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "window-width: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    // The default: one row, standard toolbar then format toolbar.
    CHECK(MainWindowProbe::side_by_side(window));
    CHECK(MainWindowProbe::format_bar_y(window) == MainWindowProbe::standard_bar_y(window));
    CHECK(MainWindowProbe::format_bar_x(window) > 0);
    const int row = minimum_width(window);
    if (row > kScreen)
      std::cerr << "one row: minimum width " << row << "\n";
    CHECK(row <= kScreen);

    // The first launch is 960 px across, and it gets that.
    int width = 0;
    int height = 0;
    window.get_size(width, height);
    if (width != 960)
      std::cerr << "first launch: " << width << "x" << height << "\n";
    CHECK(width == 960);

    // Stacked: standard above format.
    MainWindowProbe::set_side_by_side(window, false);
    settle();
    CHECK(MainWindowProbe::format_bar_y(window) > MainWindowProbe::standard_bar_y(window));
    const int stacked = minimum_width(window);
    if (stacked > kScreen)
      std::cerr << "stacked: minimum width " << stacked << "\n";
    CHECK(stacked <= kScreen);

    // And it can be made that narrow.
    window.resize(kScreen, 700);
    settle();
    window.get_size(width, height);
    CHECK(width <= kScreen);
    MainWindowProbe::set_side_by_side(window, true);
    settle();
    window.resize(kScreen, 700);
    settle();
    window.get_size(width, height);
    if (width > kScreen)
      std::cerr << "one row after resize: " << width << "\n";
    CHECK(width <= kScreen);
    window.hide();
    settle();
  }
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("window-width", kChecks);
}
