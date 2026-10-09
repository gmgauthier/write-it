/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. A closed window must leave nothing
// behind on objects that outlive it: the clipboard is the application's,
// so a clipboard change after a window is gone must not reach it (ASan
// reports the use after free), while a window still open keeps following
// the clipboard for Paste.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static bool can_paste(MainWindow& w)
  {
    return w.paste_item_ && w.paste_item_->get_sensitive();
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

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

// Takes the clipboard with `text` and lets the owner change arrive.
void clip(const char* text)
{
  Gtk::Clipboard::get()->set_text(text);
  Gtk::Clipboard::get()->store();
  settle();
}

}  // namespace

constexpr int kChecks = 4;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-window-lifetime-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "window-lifetime: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  // A window opened and closed, as File > New then closing it does.
  {
    auto first = std::make_unique<writeit::MainWindow>();
    first->show();
    settle();
    first->hide();
    settle();
  }
  settle();
  // The clipboard changes with no window: nothing may reach the old one.
  clip("after the first window closed");
  CHECK(Gtk::Clipboard::get()->wait_is_text_available());

  {
    writeit::MainWindow second;
    second.show();
    settle();
    // A window still open follows the clipboard: Paste is on with text.
    clip("while the second window is open");
    CHECK(MainWindowProbe::can_paste(second));
    second.hide();
    settle();
  }
  settle();
  // And again after the second one closes, from the stack this time.
  clip("after the second window closed");
  CHECK(Gtk::Clipboard::get()->wait_is_text_available());
  clip("once more");
  CHECK(Gtk::Clipboard::get()->wait_for_text() == "once more");

  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("window-lifetime", kChecks);
}
