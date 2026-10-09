/* SPDX-License-Identifier: Unlicense */

// The real window, under a display (CI runs the suite in xvfb-run). The text
// view sits on the page, inside the pasteboard's scroller, so its own
// scroll_mark_onscreen has nothing to scroll. This opens a letter several
// screens long and moves the caret the ways a user does (Ctrl+End, Ctrl+Home,
// the arrows, End, Page Down and Up, typing, Edit > Find…) in Page and Draft
// and at 200%, and checks after each that the pasteboard shows the caret. A
// turn of the mouse wheel may leave the caret behind.

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
#include <memory>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

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
  // Edit > Find…, type the needle, press Next, then Close; what is selected.
  static Glib::ustring find(MainWindow& w, const char* needle)
  {
    w.find_item_->activate();
    w.find_entry_->set_text(needle);
    auto* next =
        dynamic_cast<Gtk::Button*>(w.find_dialog_->get_widget_for_response(Gtk::RESPONSE_APPLY));
    if (next && next->get_label() == "Next")
      next->clicked();
    w.find_dialog_->response(Gtk::RESPONSE_CLOSE);
    Gtk::TextIter start;
    Gtk::TextIter end;
    w.buffer_->get_selection_bounds(start, end);
    return w.buffer_->get_text(start, end);
  }
  // Where follow_caret_ sits in the window, in bytes from its start.
  static std::size_t follow_offset(MainWindow& w)
  {
    return static_cast<std::size_t>(reinterpret_cast<char*>(&w.follow_caret_) -
                                    reinterpret_cast<char*>(&w));
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
  // The caret's rectangle in buffer coordinates.
  static Gdk::Rectangle caret_rect(MainWindow& w)
  {
    Gdk::Rectangle rect;
    w.text_.get_iter_location(w.buffer_->get_insert()->get_iter(), rect);
    return rect;
  }
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

int bound_offset(writeit::MainWindow& window)
{
  return MainWindowProbe::buffer(window)->get_selection_bound()->get_iter().get_offset();
}

int caret_line(writeit::MainWindow& window)
{
  return MainWindowProbe::buffer(window)->get_insert()->get_iter().get_line();
}

double page_size(writeit::MainWindow& window)
{
  return MainWindowProbe::scroller(window).get_vadjustment()->get_page_size();
}

// One Page Down or Page Up: the caret went about a screen (the pasteboard's
// visible height, give or take two lines) in the right direction, kept its
// x, and is in view; with Shift the selection's other end stayed put, and
// without it there is no selection. Five checks.
void page_key(writeit::MainWindow& window, guint keyval, bool shift, const char* where)
{
  const Gdk::Rectangle before = MainWindowProbe::caret_rect(window);
  const int anchor = shift ? bound_offset(window) : -1;
  const double sign = keyval == GDK_KEY_Page_Down ? 1 : -1;
  key(window, keyval, shift ? GDK_SHIFT_MASK : GdkModifierType(0));
  const Gdk::Rectangle after = MainWindowProbe::caret_rect(window);
  const double moved = (after.get_y() - before.get_y()) * sign;
  const double slack = 2.0 * before.get_height();
  const bool far = std::abs(moved - page_size(window)) <= slack;
  if (!far)
    std::cerr << where << ": caret moved " << moved << " px, a screen is " << page_size(window)
              << ", now on line " << caret_line(window) << "\n";
  CHECK(far);
  const bool kept_x = std::abs(after.get_x() - before.get_x()) <= 2;
  if (!kept_x)
    std::cerr << where << ": caret x " << before.get_x() << " -> " << after.get_x() << ", line "
              << caret_line(window) << "\n";
  CHECK(kept_x);
  CHECK(caret_visible(window, where));
  if (shift)
    CHECK(bound_offset(window) == anchor);
  else
    CHECK(bound_offset(window) == caret_offset(window));
  CHECK(caret_offset(window) != bound_offset(window) || !shift);
}

// Whether the pasteboard is scrolled to its very top, or its very bottom.
bool at_top(writeit::MainWindow& window, const char* where)
{
  auto v = MainWindowProbe::scroller(window).get_vadjustment();
  const bool ok = std::abs(v->get_value() - v->get_lower()) < 0.5;
  if (!ok)
    std::cerr << where << ": scrolled to " << v->get_value() << ", the top is " << v->get_lower()
              << "\n";
  return ok;
}

bool at_bottom(writeit::MainWindow& window, const char* where)
{
  auto v = MainWindowProbe::scroller(window).get_vadjustment();
  const double bottom = v->get_upper() - v->get_page_size();
  const bool ok = std::abs(v->get_value() - bottom) < 0.5;
  if (!ok)
    std::cerr << where << ": scrolled to " << v->get_value() << ", the bottom is " << bottom
              << "\n";
  return ok;
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
  for (int i = 1; i <= 200; ++i)
    rtf += "Paragraph " + std::to_string(i) + " of the long letter.\\par ";
  rtf += "Yours sincerely, Greg.}";
  return rtf;
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 179;

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

  // A first launch is at Fit width. The caret stays in view there, and when
  // a zoom takes the page from Fit width to 200% and back.
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, letter);
    settle();
    MainWindowProbe::text(window).grab_focus();
    settle();
    CHECK(caret_visible(window, "Fit width open"));
    key(window, GDK_KEY_End, ctrl);
    CHECK(caret_visible(window, "Fit width Ctrl+End"));
    MainWindowProbe::zoom(window, 200);
    settle();
    CHECK(caret_visible(window, "Fit width to 200% at the end"));
    MainWindowProbe::zoom(window, 0);
    settle();
    CHECK(caret_visible(window, "200% to Fit width at the end"));
    key(window, GDK_KEY_Home, ctrl);
    CHECK(caret_visible(window, "Fit width Ctrl+Home"));
    MainWindowProbe::zoom(window, 200);
    settle();
    CHECK(caret_visible(window, "Fit width to 200% at the top"));
    window.hide();
    settle();
  }

  // The rest is written at 100%, the zoom it sets first.
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::zoom(window, 100);
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

    // Edit > Find…: the real menu item, dialog, entry and Next button.
    key(window, GDK_KEY_Home, ctrl);
    CHECK(MainWindowProbe::find(window, "Paragraph 90 ") == "Paragraph 90 ");
    CHECK(caret_line(window) == 90);
    CHECK(caret_visible(window, "Page 100% Find down"));
    // From there, a match above it: Find wraps round to the top.
    CHECK(MainWindowProbe::find(window, "Paragraph 7 ") == "Paragraph 7 ");
    CHECK(caret_line(window) == 7);
    CHECK(caret_visible(window, "Page 100% Find wrapping up"));

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

    // Page Down and Page Up move a screen, not to the ends of the letter,
    // and scroll the pasteboard by about a screen with them.
    // From column 5 of "Paragraph 1", so every line it lands on has the
    // same letters before the caret and the same x is a character boundary.
    key(window, GDK_KEY_Home, ctrl);
    MainWindowProbe::buffer(window)->place_cursor(
        MainWindowProbe::buffer(window)->get_iter_at_line_offset(1, 5));
    settle();
    const double top = vvalue(window);
    page_key(window, GDK_KEY_Page_Down, false, "Page Down");
    CHECK(caret_line(window) < 60);
    CHECK(std::abs(vvalue(window) - top - page_size(window)) <= 2 * 21);
    page_key(window, GDK_KEY_Page_Down, false, "Page Down again");
    page_key(window, GDK_KEY_Page_Down, false, "Page Down a third time");
    page_key(window, GDK_KEY_Page_Down, true, "Shift+Page Down");
    page_key(window, GDK_KEY_Page_Up, false, "Page Up");
    page_key(window, GDK_KEY_Page_Up, true, "Shift+Page Up");
    const double before_up = vvalue(window);
    page_key(window, GDK_KEY_Page_Up, false, "Page Up again");
    CHECK(caret_line(window) > 0);
    CHECK(std::abs(before_up - vvalue(window) - page_size(window)) <= 2 * 21);

    // On the first line the pasteboard goes all the way to the top, so the
    // page's top edge and the gray above it show; on the last line, all the
    // way to the bottom. In Page and in Draft.
    for (ViewMode mode : {ViewMode::Page, ViewMode::Draft}) {
      MainWindowProbe::view(window, mode);
      settle();
      const bool page = mode == ViewMode::Page;
      key(window, GDK_KEY_End, ctrl);
      CHECK(at_bottom(window, page ? "Page Ctrl+End" : "Draft Ctrl+End"));
      key(window, GDK_KEY_Home);
      CHECK(at_bottom(window, page ? "Page Home on the last line" : "Draft Home on the last line"));
      key(window, GDK_KEY_Home, ctrl);
      CHECK(at_top(window, page ? "Page Ctrl+Home" : "Draft Ctrl+Home"));
      key(window, GDK_KEY_End);
      CHECK(at_top(window, page ? "Page End on the first line" : "Draft End on the first line"));
    }
    MainWindowProbe::view(window, ViewMode::Page);
    window.hide();
    settle();
  }

  // A window closed with a caret move still waiting on the idle leaves
  // nothing behind to run on it. The window lives in memory this test owns;
  // once it is destroyed every byte is set to a pattern (follow_caret_
  // false, so a stale callback returns before it touches anything else),
  // and a callback that ran on the dead window would have written to it.
  {
    using Storage =
        std::aligned_storage_t<sizeof(writeit::MainWindow), alignof(writeit::MainWindow)>;
    auto storage = std::make_unique<Storage>();
    auto* window = new (storage.get()) writeit::MainWindow();
    window->show();
    settle();
    MainWindowProbe::open(*window, letter);
    settle();
    const std::size_t follow = MainWindowProbe::follow_offset(*window);
    MainWindowProbe::buffer(*window)->place_cursor(
        MainWindowProbe::buffer(*window)->get_iter_at_line(60));
    window->hide();
    window->~MainWindow();
    auto* bytes = reinterpret_cast<unsigned char*>(storage.get());
    std::memset(bytes, 0x5a, sizeof(Storage));
    bytes[follow] = 0;
    std::vector<unsigned char> expect(bytes, bytes + sizeof(Storage));
    settle();
    CHECK(std::memcmp(bytes, expect.data(), sizeof(Storage)) == 0);
  }

  g_remove(letter.c_str());
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("caret-scroll", kChecks);
}
