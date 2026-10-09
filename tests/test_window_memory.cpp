/* SPDX-License-Identifier: Unlicense */

// The window's size, maximised state and zoom, in the real window under a
// display (CI runs the suite in xvfb-run). The first launch, with no saved
// setting, is 960 px wide at Fit width, and Fit width follows the window as
// it is resized. Closing the window saves its size and zoom (Fit width
// included), the next launch restores them, and a saved size larger than the
// screen is clamped to it. A corrupt, empty or missing ini is a first launch.
//
// Maximising needs a window manager, which xvfb-run does not start; what the
// window remembers of a maximised state is covered by the settings suite
// (WindowMemory).
//
// The window is driven only through its widgets, found by walking the tree.

#include "check.hpp"
#include "main_window.hpp"
#include "settings.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace {

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

void walk(Gtk::Widget& widget, const std::function<void(Gtk::Widget&)>& visit)
{
  visit(widget);
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    for (Gtk::Widget* child : container->get_children())
      walk(*child, visit);
  }
}

Gtk::MenuItem* find_item(const std::vector<Gtk::Widget*>& children, const Glib::ustring& label)
{
  for (Gtk::Widget* child : children) {
    auto* item = dynamic_cast<Gtk::MenuItem*>(child);
    if (item && item->get_label() == label)
      return item;
  }
  return nullptr;
}

// View > Zoom's item for a label ("150%", "Fit width").
Gtk::RadioMenuItem* zoom_item(Gtk::Window& window, const char* label)
{
  Gtk::RadioMenuItem* found = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar || found)
      return;
    Gtk::MenuItem* view = find_item(bar->get_children(), "_View");
    if (!view || !view->get_submenu())
      return;
    Gtk::MenuItem* zoom = find_item(view->get_submenu()->get_children(), "_Zoom");
    if (!zoom || !zoom->get_submenu())
      return;
    found =
        dynamic_cast<Gtk::RadioMenuItem*>(find_item(zoom->get_submenu()->get_children(), label));
  });
  return found;
}

Gtk::TextView* text_view(Gtk::Window& window)
{
  Gtk::TextView* found = nullptr;
  walk(window, [&found](Gtk::Widget& widget) {
    if (!found)
      found = dynamic_cast<Gtk::TextView*>(&widget);
  });
  return found;
}

// The page (the text view's frame) and the pasteboard it scrolls in.
int page_width(Gtk::Window& window)
{
  Gtk::TextView* view = text_view(window);
  return view && view->get_parent() ? view->get_parent()->get_allocated_width() : -1;
}

int pasteboard_width(Gtk::Window& window)
{
  Gtk::TextView* view = text_view(window);
  for (Gtk::Widget* up = view; up; up = up->get_parent()) {
    if (dynamic_cast<Gtk::ScrolledWindow*>(up))
      return up->get_allocated_width();
  }
  return -1;
}

// Fit width: the page is the pasteboard's width less its 18 px gutters,
// give or take a pixel of rounding.
bool fits_width(Gtk::Window& window)
{
  const int page = page_width(window);
  const int board = pasteboard_width(window);
  const bool ok = page > 0 && board > 0 && std::abs(page - (board - 36)) <= 2;
  if (!ok)
    std::cerr << "fit width: page " << page << " on a pasteboard of " << board << "\n";
  return ok;
}

std::string size_of(Gtk::Window& window)
{
  int width = 0;
  int height = 0;
  window.get_size(width, height);
  return std::to_string(width) + "x" + std::to_string(height);
}

std::string ini()
{
  return writeit::Settings::config_path();
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 38;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-window-memory-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "window-memory: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  // The first launch: no ini at all.
  CHECK(!Glib::file_test(ini(), Glib::FILE_TEST_EXISTS));
  {
    writeit::MainWindow window;
    window.show();
    settle();
    const std::string first = size_of(window);
    if (first != "960x700")
      std::cerr << "first launch: " << first << "\n";
    CHECK(first == "960x700");
    CHECK(!window.is_maximized());
    Gtk::RadioMenuItem* fit = zoom_item(window, "Fit width");
    CHECK(fit && fit->get_active());
    CHECK(fits_width(window));

    // Fit width follows the window, wider and narrower.
    const int at_960 = page_width(window);
    window.resize(1200, 700);
    settle();
    CHECK(page_width(window) > at_960);
    CHECK(fits_width(window));
    window.resize(800, 700);
    settle();
    CHECK(page_width(window) < at_960);
    CHECK(fits_width(window));

    // A zoom and a size of its own, then close: both are saved.
    Gtk::RadioMenuItem* zoom150 = zoom_item(window, "150%");
    CHECK(zoom150 != nullptr);
    if (zoom150)
      zoom150->activate();
    window.resize(1000, 650);
    settle();
    CHECK(size_of(window) == "1000x650");
    window.close();
    settle();
  }
  {
    writeit::Settings saved;
    saved.load();
    CHECK(saved.window_width == 1000);
    CHECK(saved.window_height == 650);
    CHECK(!saved.window_maximized);
    CHECK(saved.zoom == 150);
  }

  // The next launch comes back at that size and zoom. Fit width is saved too.
  {
    writeit::MainWindow window;
    window.show();
    settle();
    CHECK(size_of(window) == "1000x650");
    Gtk::RadioMenuItem* zoom150 = zoom_item(window, "150%");
    CHECK(zoom150 && zoom150->get_active());
    CHECK(page_width(window) == writeit::view_geometry(writeit::ViewMode::Page, 1.5).page_width);
    Gtk::RadioMenuItem* fit = zoom_item(window, "Fit width");
    CHECK(fit != nullptr);
    if (fit)
      fit->activate();
    settle();
    window.close();
    settle();
  }
  {
    writeit::Settings saved;
    saved.load();
    CHECK(saved.zoom == 0);
    CHECK(Glib::file_get_contents(ini()).find("zoom=fit-width") != std::string::npos);
  }
  {
    writeit::MainWindow window;
    window.show();
    settle();
    Gtk::RadioMenuItem* fit = zoom_item(window, "Fit width");
    CHECK(fit && fit->get_active());
    CHECK(fits_width(window));
    window.close();
    settle();
  }

  // A corrupt or empty ini, or one with none of the window's keys, opens as
  // the first launch does: 960 by 700 at Fit width.
  for (const char* text :
       {"\x01\x02 not an ini [ at all\n", "",
        "[write-it]\nwindow-width=wide\nwindow-height=-1\nzoom=big\n", "[write-it]\nunits=cm\n"}) {
    Glib::file_set_contents(ini(), text);
    writeit::MainWindow window;
    window.show();
    settle();
    const std::string size = size_of(window);
    if (size != "960x700")
      std::cerr << "corrupt ini: " << size << "\n";
    CHECK(size == "960x700");
    Gtk::RadioMenuItem* fit = zoom_item(window, "Fit width");
    CHECK(fit && fit->get_active());
    CHECK(fits_width(window));
    window.close();
    settle();
  }

  // A size saved on a bigger screen is clamped to this one's work area.
  Glib::file_set_contents(ini(), "[write-it]\nwindow-width=5000\nwindow-height=4000\n");
  {
    auto display = Gdk::Display::get_default();
    auto monitor = display->get_primary_monitor();
    if (!monitor)
      monitor = display->get_monitor(0);
    Gdk::Rectangle area;
    if (monitor)
      monitor->get_workarea(area);
    CHECK(area.get_width() > 0 && area.get_height() > 0);
    writeit::MainWindow window;
    window.show();
    settle();
    int width = 0;
    int height = 0;
    window.get_size(width, height);
    if (width > area.get_width() || height > area.get_height())
      std::cerr << "clamp: " << width << "x" << height << " on a work area of " << area.get_width()
                << "x" << area.get_height() << "\n";
    CHECK(width == area.get_width());
    CHECK(height <= area.get_height());
    window.close();
    settle();
  }

  g_remove(ini().c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("window-memory", kChecks);
}
