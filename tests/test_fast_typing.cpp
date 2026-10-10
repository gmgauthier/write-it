/* SPDX-License-Identifier: Unlicense */

// The real window under a display. Typing fast must not crash it. Every
// keystroke updates the menus and toolbars, and that must never wait on the
// clipboard: a wait runs a main loop of its own, which takes the next key
// press, which updates the actions again, and so on down until the stack runs
// out. Here 2,000 key presses, letters and Backspaces, are queued at once, as
// a fast typist's keyboard queues them, before the main loop runs:
// update_actions() must never run inside itself, and the text must come out
// as typed.
//
// The clipboard has no owner, as on a desktop that has not copied anything:
// asking it for its targets goes out to the X server, and the answer comes
// back later.

#include "check.hpp"
#include "main_window.hpp"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
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
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& w)
  {
    return w.buffer_;
  }
  static Gtk::TextView& text(MainWindow& w)
  {
    return w.text_;
  }
  static int depth_peak(MainWindow& w)
  {
    return w.actions_depth_peak_;
  }
  static void reset_depth_peak(MainWindow& w)
  {
    w.actions_depth_peak_ = 0;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

constexpr int kKeys = 2000;

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

Glib::ustring text(writeit::MainWindow& window)
{
  auto buffer = MainWindowProbe::buffer(window);
  return buffer->get_text(buffer->begin(), buffer->end());
}

// A key press queued on the window, as the keyboard delivers it: GTK takes
// it from the queue when the main loop next runs and gives it to the focus.
void queue_key(writeit::MainWindow& window, guint keyval)
{
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(window.get_window()->gobj()));
  event->key.send_event = TRUE;
  event->key.time = GDK_CURRENT_TIME;
  event->key.keyval = keyval;
  GdkKeymapKey* keys = nullptr;
  gint n = 0;
  if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gdk_display_get_default()),
                                        keyval, &keys, &n) &&
      n > 0) {
    event->key.hardware_keycode = static_cast<guint16>(keys[0].keycode);
    event->key.group = static_cast<guint8>(keys[0].group);
  }
  g_free(keys);
  GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
  gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
  gdk_event_put(event);
  gdk_event_free(event);
}

}  // namespace

constexpr int kChecks = 4;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-fast-typing-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "fast-typing: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  // Nobody owns the clipboard: take it, then give it up.
  Gtk::Clipboard::get()->set_text("taken");
  settle();
  Gtk::Clipboard::get()->clear();
  settle();

  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::text(window).grab_focus();
    settle();
    CHECK(text(window).empty());

    // Each five keys type a letter, then an x and a y each taken back with
    // Backspace: 2,000 keys leave 400 letters. Every keystroke looks at the
    // whole document, so a short one keeps the 2,000 quick.
    std::string typed;
    for (int i = 0; i < kKeys; ++i) {
      switch (i % 5) {
        case 0: {
          const char c = static_cast<char>('a' + (i / 5) % 26);
          typed += c;
          queue_key(window, static_cast<guint>(c));
          break;
        }
        case 1:
          queue_key(window, GDK_KEY_x);
          break;
        case 3:
          queue_key(window, GDK_KEY_y);
          break;
        default:
          queue_key(window, GDK_KEY_BackSpace);
          break;
      }
    }
    MainWindowProbe::reset_depth_peak(window);
    settle();
    while (gdk_events_pending())
      settle();

    const int peak = MainWindowProbe::depth_peak(window);
    if (peak > 1)
      std::cerr << "update_actions() ran " << peak << " deep\n";
    CHECK(peak == 1);
    const Glib::ustring got = text(window);
    if (got.raw() != typed)
      std::cerr << "typed " << typed.size() << " characters, got " << got.bytes() << " bytes\n";
    CHECK(got.raw() == typed);
    CHECK(gtk_main_level() == 0);
    window.hide();
    settle();
  }

  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("fast-typing", kChecks);
}
