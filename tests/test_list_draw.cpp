/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. Drawing the list labels must not cost
// a keystroke more as more items show. GTK keeps only one laid-out line
// outside its own draw pass, so each get_iter_location() on a label's line
// lays that line out and shapes it again; and a label's Pango layout shaped
// afresh on every draw costs as much again. So a key typed in a list item,
// and Enter, may read a few labels' geometry and build a layout or two, a
// small number however many items are in view: checked in a small window
// and in a large one with far more items showing. Typing in one item builds
// no label layout at all: its own label is unchanged, and no other item's
// label is shaped again.

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
  static long locates(MainWindow& w)
  {
    return w.label_locates_;
  }
  static long layouts(MainWindow& w)
  {
    return w.label_layouts_;
  }
  // List items whose line is at least partly in view.
  static int visible_items(MainWindow& w)
  {
    // The text view is the whole page; the pasteboard's scroller shows part.
    int x = 0;
    int y = 0;
    w.paste_.translate_coordinates(w.text_, 0, 0, x, y);
    int top = 0;
    int bottom = 0;
    w.text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, y, x, top);
    w.text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, 0, y + w.paste_.get_allocated_height(),
                                    x, bottom);
    int count = 0;
    for (const auto& line : w.list_lines()) {
      if (line.format.list.kind == ListKind::None)
        continue;
      int line_y = 0;
      int height = 0;
      w.text_.get_line_yrange(w.buffer_->get_iter_at_line(line.line), line_y, height);
      if (line_y + height >= top && line_y <= bottom)
        ++count;
    }
    return count;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

constexpr int kItems = 300;
constexpr int kKeys = 4;
// Per keystroke, however many items show.
constexpr long kLocatesPerKey = 4;
// Typing in one item leaves every label's text and font as they were.
constexpr long kLayoutsPerKey = 0;
// Enter splits an item and renumbers every item below it.
constexpr long kLocatesPerEnter = 8;
constexpr long kLayoutsPerEnter = 3;

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
  settle();
}

writeit::Document numbered_list()
{
  writeit::Document doc;
  for (int i = 0; i < kItems; ++i) {
    writeit::Paragraph p;
    writeit::Run run;
    run.text = "Item " + std::to_string(i + 1) + " of a long numbered list.";
    if (i % 5 == 2)
      run.bold = true;
    p.runs.push_back(run);
    p.list.kind = writeit::ListKind::Number;
    p.list.list = 1;
    p.list.level = i % 7 == 3 ? 1 : 0;
    p.indents = writeit::list_indents(p.list.level);
    doc.paragraphs.push_back(p);
  }
  return doc;
}

// Types kKeys characters and then Enter in item 3 of a window `width` by
// `height`; returns how many list items were in view. Five checks.
int typing(writeit::MainWindow& window, int width, int height, const char* what)
{
  window.resize(width, height);
  settle();
  settle();
  MainWindowProbe::place_at_end(window, 3);
  settle();
  const int shown = MainWindowProbe::visible_items(window);
  int draws = 0;
  auto counting = MainWindowProbe::text(window).signal_draw().connect(
      [&draws](const Cairo::RefPtr<Cairo::Context>&) {
        ++draws;
        return false;
      },
      false);
  const long locates = MainWindowProbe::locates(window);
  const long layouts = MainWindowProbe::layouts(window);
  for (int i = 0; i < kKeys; ++i)
    press(window, GDK_KEY_k);
  const long typed_locates = MainWindowProbe::locates(window) - locates;
  const long typed_layouts = MainWindowProbe::layouts(window) - layouts;
  std::cout << "list-draw: " << what << ", " << shown << " items in view, " << draws
            << " draws, per key " << typed_locates / double(kKeys) << " label locates, "
            << typed_layouts / double(kKeys) << " label layouts\n";
  CHECK(draws >= kKeys);
  CHECK(typed_locates <= kLocatesPerKey * kKeys);
  CHECK(typed_layouts <= kLayoutsPerKey * kKeys);
  const long enter_locates = MainWindowProbe::locates(window);
  const long enter_layouts = MainWindowProbe::layouts(window);
  press(window, GDK_KEY_Return);
  const long entered_locates = MainWindowProbe::locates(window) - enter_locates;
  const long entered_layouts = MainWindowProbe::layouts(window) - enter_layouts;
  std::cout << "list-draw: " << what << ", Enter: " << entered_locates << " label locates, "
            << entered_layouts << " label layouts\n";
  CHECK(entered_locates <= kLocatesPerEnter);
  CHECK(entered_layouts <= kLayoutsPerEnter);
  counting.disconnect();
  return shown;
}

constexpr int kChecks = 2 * 5 + 1;

}  // namespace

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-list-draw-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "list-draw: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  const std::string path = Glib::build_filename(home, "list.rtf");
  Glib::file_set_contents(path, writeit::rtf_export(numbered_list()));
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::open(window, path);
    settle();
    const int small = typing(window, 700, 420, "700x420");
    const int large = typing(window, 1400, 1200, "1400x1200");
    // The large window shows far more items, so a cost per item would show.
    CHECK(large >= small + 8);
    window.hide();
    settle();
  }
  g_remove(path.c_str());
  g_rmdir(home.c_str());
  return suite_test::done("list-draw", kChecks);
}
