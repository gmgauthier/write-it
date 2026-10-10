/* SPDX-License-Identifier: Unlicense */

// The real window under a display. Paste is on while the clipboard holds
// text and off while it holds none, and the window learns which without
// waiting on the clipboard: it asks, and the answer arrives through the main
// loop. So Paste follows a change once the loop has run.
//
// An answer can arrive after the window that asked has closed: the
// clipboard is the application's and outlives every window. A window closed
// with a question in flight must not be reached by the answer (ASan reports
// the use after free).

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
  static bool paste(MainWindow& w)
  {
    return w.paste_item_ && w.paste_item_->get_sensitive();
  }
  static bool paste_tool(MainWindow& w)
  {
    return w.paste_tool_ && w.paste_tool_->get_sensitive();
  }
  static bool context_paste(MainWindow& w)
  {
    return w.context_paste_ && w.context_paste_->get_sensitive();
  }
  static bool all_paste(MainWindow& w, bool on)
  {
    return paste(w) == on && paste_tool(w) == on && context_paste(w) == on;
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

}  // namespace

constexpr int kChecks = 8;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-clipboard-paste-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "clipboard-paste: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  auto clipboard = Gtk::Clipboard::get();

  // Nobody owns the clipboard to begin with.
  clipboard->set_text("taken");
  settle();
  clipboard->clear();
  settle();

  {
    writeit::MainWindow window;
    window.show();
    settle();
    // An empty clipboard: Paste is off, in the menu, toolbar and context menu.
    CHECK(MainWindowProbe::all_paste(window, false));

    // Text on the clipboard turns Paste on once the loop has run.
    clipboard->set_text("some text");
    settle();
    CHECK(MainWindowProbe::all_paste(window, true));

    // Clearing it turns Paste off again.
    clipboard->clear();
    settle();
    CHECK(MainWindowProbe::all_paste(window, false));

    // And back on.
    clipboard->set_text("more text");
    settle();
    CHECK(MainWindowProbe::all_paste(window, true));
    clipboard->clear();
    settle();
    CHECK(MainWindowProbe::all_paste(window, false));
    window.hide();
    settle();
  }
  settle();

  // A window that asks the clipboard as it opens, and closes before the
  // answer: the clipboard has no owner, so the answer comes from the X server
  // through the main loop, after the window is gone.
  for (int i = 0; i < 3; ++i) {
    auto window = std::make_unique<writeit::MainWindow>();
    window->show();
    window.reset();
    settle();
  }
  // The same with the clipboard changing under it: the owner change asks
  // again, and the window closes before either answer arrives.
  {
    auto window = std::make_unique<writeit::MainWindow>();
    window->show();
    settle();
    clipboard->set_text("in flight");
    clipboard->clear();
    window->hide();
    window.reset();
    settle();
  }
  // A window opened afterwards still follows the clipboard.
  {
    writeit::MainWindow window;
    window.show();
    settle();
    CHECK(MainWindowProbe::all_paste(window, false));
    clipboard->set_text("after");
    settle();
    CHECK(MainWindowProbe::all_paste(window, true));
    window.hide();
    settle();
  }
  CHECK(clipboard->wait_for_text() == "after");

  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("clipboard-paste", kChecks);
}
