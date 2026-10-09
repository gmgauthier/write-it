/* SPDX-License-Identifier: Unlicense */

// The real window, under a display (CI runs the suite in xvfb-run). The text
// view sits on the page, inside the pasteboard's scroller, so its own
// scroll_mark_onscreen has nothing to scroll. This opens a letter several
// screens long and moves the caret the ways a user does (Ctrl+End, Ctrl+Home,
// the arrows, End, typing, a jump from Find) in Page and Draft and at 200%,
// and checks after each that the pasteboard shows the caret, across as well
// as down. A turn of the mouse wheel may leave the caret behind.

#include "check.hpp"
#include "main_window.hpp"
#include "view.hpp"

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
  static Gtk::TextView& text(MainWindow& w)
  {
    return w.text_;
  }
  static Gtk::ScrolledWindow& scroller(MainWindow& w)
  {
    return w.paste_;
  }
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& w)
  {
    return w.buffer_;
  }
  // The caret's rectangle in the scrolled board's coordinates, the space
  // the pasteboard's adjustments measure.
  static bool caret_on_board(MainWindow& w, Gdk::Rectangle& out)
  {
    Gdk::Rectangle rect;
    w.text_.get_iter_location(w.buffer_->get_insert()->get_iter(), rect);
    int wx = 0;
    int wy = 0;
    w.text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, rect.get_x(), rect.get_y(), wx, wy);
    int bx = 0;
    int by = 0;
    if (!w.text_.translate_coordinates(w.board_, wx, wy, bx, by))
      return false;
    out = Gdk::Rectangle(bx, by, std::max(1, rect.get_width()), rect.get_height());
    return true;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;
using writeit::ViewMode;

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

// A key press as the keyboard delivers it to the focused text view, so the
// window's own key handler and GTK's key bindings both see it.
void key(writeit::MainWindow& window, guint keyval, GdkModifierType state = GdkModifierType(0))
{
  auto& text = MainWindowProbe::text(window);
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(text.get_window(Gtk::TEXT_WINDOW_TEXT)->gobj()));
  event->key.send_event = TRUE;
  event->key.time = GDK_CURRENT_TIME;
  event->key.state = state;
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
  gtk_widget_event(GTK_WIDGET(text.gobj()), event);
  gdk_event_free(event);
  settle();
}

// One notch of the mouse wheel over the pasteboard.
void wheel(writeit::MainWindow& window, GdkScrollDirection direction)
{
  auto& scroller = MainWindowProbe::scroller(window);
  GdkEvent* event = gdk_event_new(GDK_SCROLL);
  event->scroll.window = GDK_WINDOW(g_object_ref(scroller.get_window()->gobj()));
  event->scroll.send_event = TRUE;
  event->scroll.time = GDK_CURRENT_TIME;
  event->scroll.direction = direction;
  event->scroll.x = 100;
  event->scroll.y = 100;
  GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
  gdk_event_set_device(event, gdk_seat_get_pointer(seat));
  gtk_widget_event(GTK_WIDGET(scroller.gobj()), event);
  gdk_event_free(event);
  settle();
}

double vvalue(writeit::MainWindow& window)
{
  return MainWindowProbe::scroller(window).get_vadjustment()->get_value();
}

// Whether the pasteboard's visible area contains the whole caret.
bool caret_visible(writeit::MainWindow& window, const char* where)
{
  Gdk::Rectangle caret;
  const bool placed = MainWindowProbe::caret_on_board(window, caret);
  auto& scroller = MainWindowProbe::scroller(window);
  auto v = scroller.get_vadjustment();
  auto h = scroller.get_hadjustment();
  const double top = v->get_value();
  const double bottom = top + v->get_page_size();
  const double left = h->get_value();
  const double right = left + h->get_page_size();
  const bool ok = placed && caret.get_y() >= top && caret.get_y() + caret.get_height() <= bottom &&
                  caret.get_x() >= left && caret.get_x() + caret.get_width() <= right;
  if (!ok)
    std::cerr << where << ": caret at (" << caret.get_x() << ", " << caret.get_y() << ") "
              << caret.get_width() << "x" << caret.get_height() << ", view x " << left << "-"
              << right << " y " << top << "-" << bottom << " of " << v->get_upper() << "\n";
  return ok;
}

int caret_offset(writeit::MainWindow& window)
{
  return MainWindowProbe::buffer(window)->get_insert()->get_iter().get_offset();
}

int char_count(writeit::MainWindow& window)
{
  return MainWindowProbe::buffer(window)->get_char_count();
}

std::string long_letter()
{
  std::string rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0\\fs22 ";
  // A first paragraph that wraps, so End and Home have a line to cross.
  rtf +=
      "Dear Mrs Hart, thank you for the letter about the parish fete, the bunting, the "
      "raffle and the tombola stall\\par ";
  for (int i = 1; i <= 120; ++i)
    rtf += "Paragraph " + std::to_string(i) + " of the long letter.\\par ";
  rtf += "Yours sincerely, Greg.}";
  return rtf;
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 120;

int main(int argc, char* argv[])
{
  // A private config folder, so the test never touches the user's ini.
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-caret-scroll-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "caret-scroll: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  const std::string letter = Glib::build_filename(home, "long.rtf");
  Glib::file_set_contents(letter, long_letter());
  const auto ctrl = GDK_CONTROL_MASK;

  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, letter);
    settle();
    MainWindowProbe::text(window).grab_focus();
    settle();

    // The letter is longer than the window, and opens at its top.
    CHECK(MainWindowProbe::scroller(window).get_vadjustment()->get_upper() >
          2 * MainWindowProbe::scroller(window).get_vadjustment()->get_page_size());
    CHECK(caret_visible(window, "Page 100% open"));

    // Ctrl+End, the reported case, and back with Ctrl+Home.
    key(window, GDK_KEY_End, ctrl);
    CHECK(caret_offset(window) == char_count(window));
    CHECK(vvalue(window) > 0);
    CHECK(caret_visible(window, "Page 100% Ctrl+End"));
    key(window, GDK_KEY_Home, ctrl);
    CHECK(caret_offset(window) == 0);
    CHECK(caret_visible(window, "Page 100% Ctrl+Home"));

    // Down a line at a time, past the bottom of the window.
    for (int i = 0; i < 40; ++i) {
      key(window, GDK_KEY_Down);
      CHECK(caret_visible(window, "Page 100% Down"));
    }
    // And up again from the end.
    key(window, GDK_KEY_End, ctrl);
    for (int i = 0; i < 40; ++i) {
      key(window, GDK_KEY_Up);
      CHECK(caret_visible(window, "Page 100% Up"));
    }

    // A caret placed by the program, as Find and Undo place it.
    key(window, GDK_KEY_Home, ctrl);
    MainWindowProbe::buffer(window)->place_cursor(
        MainWindowProbe::buffer(window)->get_iter_at_line(90));
    settle();
    CHECK(caret_visible(window, "Page 100% placed"));

    // Typing new paragraphs at the end grows the page under the caret.
    key(window, GDK_KEY_End, ctrl);
    for (int i = 0; i < 20; ++i) {
      key(window, GDK_KEY_Return);
      key(window, GDK_KEY_x);
      CHECK(caret_visible(window, "Page 100% typing"));
    }

    // The wheel scrolls away from the caret and the caret lets it.
    const double before = vvalue(window);
    for (int i = 0; i < 5; ++i)
      wheel(window, GDK_SCROLL_UP);
    CHECK(vvalue(window) < before);
    CHECK(!caret_visible(window, "Page 100% after the wheel (hidden is right)"));
    // The next movement brings it back.
    key(window, GDK_KEY_Left);
    CHECK(caret_visible(window, "Page 100% Left after the wheel"));

    // 200%: a taller page, and End and Home along its first line.
    MainWindowProbe::zoom(window, 200);
    settle();
    CHECK(caret_visible(window, "Page 200% after zoom"));
    key(window, GDK_KEY_Home, ctrl);
    CHECK(caret_visible(window, "Page 200% Ctrl+Home"));
    key(window, GDK_KEY_End);
    CHECK(caret_visible(window, "Page 200% End"));
    key(window, GDK_KEY_Home);
    CHECK(caret_visible(window, "Page 200% Home"));
    key(window, GDK_KEY_End, ctrl);
    CHECK(caret_visible(window, "Page 200% Ctrl+End"));

    // Draft scrolls in the same pasteboard.
    MainWindowProbe::zoom(window, 100);
    MainWindowProbe::view(window, ViewMode::Draft);
    settle();
    key(window, GDK_KEY_Home, ctrl);
    CHECK(caret_visible(window, "Draft 100% Ctrl+Home"));
    key(window, GDK_KEY_End, ctrl);
    CHECK(caret_visible(window, "Draft 100% Ctrl+End"));
    key(window, GDK_KEY_Home, ctrl);
    CHECK(caret_visible(window, "Draft 100% Ctrl+Home again"));

    // Back to Page with the caret at the end: the switch keeps it in view.
    key(window, GDK_KEY_End, ctrl);
    MainWindowProbe::view(window, ViewMode::Page);
    settle();
    CHECK(caret_visible(window, "Page 100% after Draft"));
    window.hide();
    settle();
  }

  g_remove(letter.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("caret-scroll", kChecks);
}
