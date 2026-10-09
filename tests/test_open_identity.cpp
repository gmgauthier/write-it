/* SPDX-License-Identifier: Unlicense */

// "Already open" is decided by the file, not by the name it is asked for by.
// A symlink, a hard link, or a path through ".." to a file open in a window
// brings that window forward, whether the file comes from the command line
// or a second launch (Application::open, as the desktop file's %F does) or
// from File > Open… in another window.
//
// The real application and windows under a display (CI runs the suite in
// xvfb-run). NON_UNIQUE, so the test never hands its files to another
// Write-It on the session bus.

#include "application.hpp"
#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>
#include <unistd.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 6; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(10000);
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

Gtk::MenuItem* menu_item(Gtk::Window& window, const char* menu, const char* label)
{
  Gtk::MenuItem* found = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar || found)
      return;
    Gtk::MenuItem* top = find_item(bar->get_children(), menu);
    if (top && top->get_submenu())
      found = find_item(top->get_submenu()->get_children(), label);
  });
  return found;
}

// The visible main windows' titles, in the application's order.
std::vector<std::string> titles(const Glib::RefPtr<Gtk::Application>& app)
{
  std::vector<std::string> out;
  for (Gtk::Window* window : app->get_windows()) {
    if (dynamic_cast<writeit::MainWindow*>(window) && window->get_visible())
      out.push_back(window->get_title());
  }
  return out;
}

writeit::MainWindow* window_titled(const Glib::RefPtr<Gtk::Application>& app,
                                   const std::string& title)
{
  for (Gtk::Window* window : app->get_windows()) {
    auto* main = dynamic_cast<writeit::MainWindow*>(window);
    if (main && main->get_visible() && main->get_title() == title)
      return main;
  }
  return nullptr;
}

void open(const Glib::RefPtr<Gtk::Application>& app, const std::string& path)
{
  app->open(Gio::File::create_for_path(path));
  settle();
}

// Picks `path` in the next Open chooser, from inside its run() loop; any
// other dialog is counted and cancelled.
struct Chooser {
  std::string path;
  int choosers = 0;
  int other = 0;
  int wait = 0;
  sigc::connection tick;

  void start()
  {
    tick = Glib::signal_timeout().connect(
        [this] {
          poll();
          return true;
        },
        20);
  }
  void poll()
  {
    for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
      if (!top->get_visible())
        continue;
      if (auto* chooser = dynamic_cast<Gtk::FileChooserDialog*>(top)) {
        if (wait == 0) {
          ++choosers;
          chooser->select_filename(path);
        }
        // The chooser ignores Open until its folder has loaded.
        if (++wait > 25) {
          wait = 0;
          chooser->response(Gtk::RESPONSE_ACCEPT);
        }
        return;
      }
      if (auto* dialog = dynamic_cast<Gtk::Dialog*>(top)) {
        ++other;
        dialog->response(Gtk::RESPONSE_CANCEL);
        return;
      }
    }
  }
};

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 16;

int main(int argc, char* argv[])
{
  std::string dir = Glib::build_filename(Glib::get_tmp_dir(), "write-it-identity-XXXXXX");
  if (!g_mkdtemp(&dir[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", dir.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "open-identity: no display, skipped\n";
    return 77;
  }
  const std::string a = Glib::build_filename(dir, "a.rtf");
  const std::string b = Glib::build_filename(dir, "b.rtf");
  const std::string sym = Glib::build_filename(dir, "link.rtf");
  const std::string hard = Glib::build_filename(dir, "hard.rtf");
  const std::string sub = Glib::build_filename(dir, "sub");
  const std::string rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\pard\\f0\\fs22 Text.\\par}";
  Glib::file_set_contents(a, rtf);
  Glib::file_set_contents(b, rtf);
  g_mkdir(sub.c_str(), 0700);
  CHECK(symlink("a.rtf", sym.c_str()) == 0 && link(a.c_str(), hard.c_str()) == 0);

  // A dialog nobody answers would hang the suite: fail instead.
  Glib::signal_timeout().connect_seconds(
      [] {
        std::cerr << "open-identity: stuck\n";
        std::exit(EXIT_FAILURE);
        return false;
      },
      45);

  Glib::RefPtr<Gtk::Application> app = writeit::Application::create();
  app->set_flags(app->get_flags() | Gio::APPLICATION_NON_UNIQUE);
  app->register_application();
  {
    using V = std::vector<std::string>;
    // The command line, a second launch, Open With: one window per file.
    open(app, a);
    CHECK(titles(app) == V{"Write-It - a.rtf"});
    open(app, sym);
    CHECK(titles(app) == V{"Write-It - a.rtf"});
    open(app, hard);
    CHECK(titles(app) == V{"Write-It - a.rtf"});
    open(app, sub + "/../a.rtf");
    CHECK(titles(app) == V{"Write-It - a.rtf"});
    open(app, dir + "/./link.rtf");
    CHECK(titles(app) == V{"Write-It - a.rtf"});
    // Another file is another window.
    open(app, b);
    CHECK(titles(app).size() == 2);
    CHECK(window_titled(app, "Write-It - b.rtf") != nullptr);

    // File > Open… in b's window, of a's hard link and symlink: a's window
    // comes forward, and b's keeps b.
    writeit::MainWindow* in_b = window_titled(app, "Write-It - b.rtf");
    Gtk::MenuItem* open_item = in_b ? menu_item(*in_b, "_File", "_Open…") : nullptr;
    CHECK(open_item != nullptr);
    Chooser chooser;
    chooser.start();
    for (const std::string& name : {hard, sym}) {
      chooser.path = name;
      if (open_item)
        open_item->activate();
      settle();
      CHECK(window_titled(app, "Write-It - b.rtf") == in_b);
      CHECK(window_titled(app, "Write-It - a.rtf") != nullptr);
      CHECK(titles(app).size() == 2);
    }
    CHECK(chooser.choosers == 2 && chooser.other == 0);
    chooser.tick.disconnect();
    for (Gtk::Window* window : app->get_windows())
      window->hide();
    settle();
  }
  for (const std::string& file : {sym, hard, a, b})
    g_remove(file.c_str());
  g_rmdir(sub.c_str());
  g_remove(Glib::build_filename(dir, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(dir, "write-it").c_str());
  g_rmdir(dir.c_str());
  return suite_test::done("open-identity", kChecks);
}
