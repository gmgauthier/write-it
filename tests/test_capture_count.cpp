/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. capture() copies the whole document, so
// a keystroke must not call it for the title, the list labels it draws, or
// the centred-item shifts: in a 1,000-paragraph document each copy costs a
// keystroke real time. Typing one character may capture only for undo's
// snapshot and its compare (on_user_begin and on_user_end, until undo
// records operations instead), so at most 2 in all, and a redraw with
// nothing changed captures nothing.
//
// Each document is opened, the caret placed in the middle, and a key pressed
// as the keyboard sends it; the count covers every idle and draw it causes.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
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
  static Gtk::TextView& text(MainWindow& w)
  {
    return w.text_;
  }
  static void place_at_end(MainWindow& w, int line)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    iter.forward_to_line_end();
    w.buffer_->place_cursor(iter);
  }
  static long captures(MainWindow& w)
  {
    return w.captures_;
  }
  static Glib::ustring line_text(MainWindow& w, int line)
  {
    auto start = w.buffer_->get_iter_at_line(line);
    auto end = start;
    end.forward_to_line_end();
    return w.buffer_->get_text(start, end);
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

constexpr int kParagraphs = 200;

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

// A key press queued on the window, as the keyboard delivers it.
void press(writeit::MainWindow& window, guint keyval)
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

enum class Kind { Plain, Numbered, Centred };

writeit::Document document(Kind kind)
{
  writeit::Document doc;
  for (int i = 0; i < kParagraphs; ++i) {
    writeit::Paragraph p;
    writeit::Run run;
    run.text = "Paragraph " + std::to_string(i + 1) + " holds an ordinary sentence of text.";
    p.runs.push_back(run);
    if (kind != Kind::Plain) {
      p.list.kind = writeit::ListKind::Number;
      p.list.list = 1;
      p.indents = writeit::list_indents(0);
    }
    if (kind == Kind::Centred)
      p.align = writeit::Align::Center;
    doc.paragraphs.push_back(p);
  }
  return doc;
}

const char* name_of(Kind kind)
{
  return kind == Kind::Plain ? "plain" : kind == Kind::Numbered ? "numbered" : "centred list";
}

void measure(writeit::MainWindow& window, const std::string& home, Kind kind)
{
  const std::string path = Glib::build_filename(home, std::string(name_of(kind)) + ".rtf");
  Glib::file_set_contents(path, writeit::rtf_export(document(kind)));
  MainWindowProbe::open(window, path);
  settle();
  MainWindowProbe::text(window).grab_focus();
  MainWindowProbe::place_at_end(window, kParagraphs / 2);
  settle();

  int draws = 0;
  auto counting = MainWindowProbe::text(window).signal_draw().connect(
      [&draws](const Cairo::RefPtr<Cairo::Context>&) {
        ++draws;
        return false;
      },
      false);

  // One character typed.
  long before = MainWindowProbe::captures(window);
  press(window, GDK_KEY_x);
  settle();
  const long typed = MainWindowProbe::captures(window) - before;
  std::cout << "  " << name_of(kind) << ": one key, " << typed << " captures, " << draws
            << " draws\n";
  CHECK(MainWindowProbe::line_text(window, kParagraphs / 2).raw().back() == 'x');
  CHECK(typed <= 2);

  // A redraw with nothing changed.
  draws = 0;
  before = MainWindowProbe::captures(window);
  MainWindowProbe::text(window).queue_draw();
  settle();
  const long redrawn = MainWindowProbe::captures(window) - before;
  std::cout << "  " << name_of(kind) << ": redraw, " << redrawn << " captures, " << draws
            << " draws\n";
  CHECK(draws >= 1);
  CHECK(redrawn == 0);
  counting.disconnect();
  g_remove(path.c_str());
}

}  // namespace

constexpr int kChecks = 12;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-capture-count-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "capture-count: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  // A window each: the one typed in has changes, and Open would ask.
  for (Kind kind : {Kind::Plain, Kind::Numbered, Kind::Centred}) {
    writeit::MainWindow window;
    window.set_default_size(900, 700);
    window.show();
    settle();
    measure(window, home, kind);
    window.hide();
    settle();
  }
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("capture-count", kChecks);
}
