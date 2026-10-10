/* SPDX-License-Identifier: Unlicense */

// The real window, under a display (CI runs the suite in xvfb-run). In Page
// view the white sheet is A4, 21 x 29.7 cm: at every zoom it is the zoom's
// sheet width across and at least the A4 proportion of that width tall,
// the same height the status bar's page count takes for a page. Text longer
// than a page makes the sheet taller. Its size depends only on the view,
// the zoom, the window's width and the text, never on the zooms before: 50%
// reached from Fit width, from 100% or from 75% is the same sheet, for a
// short letter and for one longer than a page.

#include "check.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cmath>
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
  static double zoom_factor(MainWindow& w)
  {
    return w.zoom_factor();
  }
  static int page_width(MainWindow& w)
  {
    return w.page_.get_allocated_width();
  }
  static int page_height(MainWindow& w)
  {
    return w.page_.get_allocated_height();
  }
  // The laid-out text's height, its top and bottom insets included.
  static int text_height(MainWindow& w)
  {
    Gtk::TextIter end = w.buffer_->end();
    int y = 0;
    int height = 0;
    w.text_.get_line_yrange(end, y, height);
    return y + height + w.text_.get_top_margin() + w.text_.get_bottom_margin();
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

const char kShort[] =
    "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0\\fs22 Dear Mrs Hart,\\par Thank you for the "
    "letter.\\par Yours sincerely, Greg.\\par}";

// About three pages at any zoom: the text scales with the page.
std::string long_letter()
{
  std::string rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0\\fs22 Dear Mrs Hart,\\par ";
  for (int i = 1; i <= 100; ++i)
    rtf += "Paragraph " + std::to_string(i) + " of the long letter.\\par ";
  rtf += "Yours sincerely, Greg.}";
  return rtf;
}

void settle()
{
  auto context = Glib::MainContext::get_default();
  // Layout runs in idles and in the frame clock; give both a few rounds.
  for (int round = 0; round < 8; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

const char* label(int percent)
{
  switch (percent) {
    case 50:
      return "50%";
    case 75:
      return "75%";
    case 100:
      return "100%";
    case 150:
      return "150%";
    case 200:
      return "200%";
    default:
      return "Fit width";
  }
}

struct Sheet {
  int width = 0;
  int height = 0;
};

Sheet go(writeit::MainWindow& window, int percent)
{
  MainWindowProbe::zoom(window, percent);
  settle();
  return {MainWindowProbe::page_width(window), MainWindowProbe::page_height(window)};
}

// The sheet at a zoom: the zoom's width across; for a short letter exactly
// the A4 height, the proportion of that width, and for a long one as tall
// as its text. Four checks.
void expect_sheet(writeit::MainWindow& window, int percent, bool long_text, const char* doc)
{
  const Sheet sheet = go(window, percent);
  const double z = MainWindowProbe::zoom_factor(window);
  const auto want = writeit::view_geometry(writeit::ViewMode::Page, z);
  const double a4 = sheet.width * 29.7 / 21.0;
  const int text = MainWindowProbe::text_height(window);
  const bool ok_width = sheet.width == want.page_width;
  // A4 to the pixel, give or take the rounding of width and height apart.
  const bool ok_a4 = std::abs(want.page_height - a4) <= 1.5;
  // The page count's page is this sheet less the page's top and bottom insets.
  const bool ok_count = writeit::page_text_height(z) == want.page_height - 2 * want.margin_y;
  // A long letter's sheet is as tall as its text, no taller: a sheet left
  // over from an earlier zoom is blank white below the last line.
  const bool ok_height = long_text
                             ? sheet.height > want.page_height && std::abs(sheet.height - text) <= 2
                             : sheet.height == want.page_height;
  if (!(ok_width && ok_a4 && ok_count && ok_height))
    std::cerr << doc << " " << label(percent) << ": sheet " << sheet.width << "x" << sheet.height
              << " (want " << want.page_width << " across, least " << want.page_height << ", A4 "
              << a4 << "), text " << text << ", a page holds " << writeit::page_text_height(z)
              << "\n";
  CHECK(ok_width);
  CHECK(ok_a4);
  CHECK(ok_count);
  CHECK(ok_height);
}

// 50% from Fit width, 100% and 75%: one sheet, the same as 50% from 50%.
// Four checks.
void expect_history_free(writeit::MainWindow& window, const char* doc)
{
  Sheet seen[3];
  const int from[] = {0, 100, 75};
  for (int i = 0; i < 3; ++i) {
    go(window, from[i]);
    seen[i] = go(window, 50);
    std::cerr << doc << ": " << label(from[i]) << " to 50%: " << seen[i].width << "x"
              << seen[i].height << "\n";
  }
  CHECK(seen[1].width == seen[0].width);
  CHECK(seen[1].height == seen[0].height);
  CHECK(seen[2].width == seen[0].width);
  CHECK(seen[2].height == seen[0].height);
}

void run(const std::string& path, bool long_text, const char* doc)
{
  writeit::MainWindow window;
  window.show();
  settle();
  MainWindowProbe::open(window, path);
  settle();
  for (int percent : {50, 75, 100, 200, 0, 50, 200, 75, 0, 100})
    expect_sheet(window, percent, long_text, doc);
  expect_history_free(window, doc);
  // And back up: every zoom after 50% is the sheet it was the first time.
  for (int percent : {75, 100, 200, 0})
    expect_sheet(window, percent, long_text, doc);
  window.hide();
  settle();
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 120;

int main(int argc, char* argv[])
{
  // A private config folder, so the test never touches the user's ini.
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-page-sheet-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "page-sheet: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  const std::string short_path = Glib::build_filename(home, "short.rtf");
  const std::string long_path = Glib::build_filename(home, "long.rtf");
  Glib::file_set_contents(short_path, kShort);
  Glib::file_set_contents(long_path, long_letter());

  run(short_path, false, "short letter");
  run(long_path, true, "long letter");

  g_remove(short_path.c_str());
  g_remove(long_path.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("page-sheet", kChecks);
}
