/* SPDX-License-Identifier: Unlicense */

// Half-point sizes and the size of empty lines in the real window, under a
// display (CI runs the suite in xvfb-run). Bug Basher's live pass found
// \fs21 text shown and saved as 10 pt, and an empty 20 pt line reopening at
// the default size. Here a file with both is opened through the window: the
// size box shows 10.5 and 8.5 as Word 97 does, typing on the empty line
// uses 20 pt, the line is drawn at 20 pt, 10.5 can be typed into the box,
// Enter keeps the size of the line it splits, and Save writes the file back
// byte for byte.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static bool open(MainWindow& w, const std::string& path)
  {
    return w.open_path(path, MainWindow::OpenKind::Rtf);
  }
  static bool save(MainWindow& w)
  {
    return w.save_document();
  }
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& w)
  {
    return w.buffer_;
  }
  static Gtk::TextView& text(MainWindow& w)
  {
    return w.text_;
  }
  static Gtk::ComboBoxText& size_combo(MainWindow& w)
  {
    return w.size_combo_;
  }
  static Run format_at(MainWindow& w, int offset)
  {
    return w.format_of(w.buffer_->get_iter_at_offset(offset));
  }
  static Document capture(MainWindow& w)
  {
    return w.capture();
  }
  static bool dirty(MainWindow& w)
  {
    return w.dirty();
  }
  static void undo(MainWindow& w)
  {
    w.undo();
  }
  static size_t undo_steps(MainWindow& w)
  {
    return w.undo_.size();
  }
  static Gtk::MenuItem* bold_item(MainWindow& w)
  {
    return w.bold_item_;
  }
  static Gtk::MenuItem* italic_item(MainWindow& w)
  {
    return w.italic_item_;
  }
  static Gtk::MenuItem* underline_item(MainWindow& w)
  {
    return w.underline_item_;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 4; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(10000);
  }
  while (context->pending())
    context->iteration(false);
}

const std::string kFile =
    "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl{\\f0\\fswiss Sans;}}\n"
    "\\pard\\f0\\fs21\\b0\\i0\\ulnone Half\\par\n"
    "\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n"
    "\\pard\\f0\\fs17\\b0\\i0\\ulnone Small\\par\n"
    "\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n"
    "}";

// Offsets in the buffer "Half\n\nSmall\n": the empty 20 pt line's newline
// is at 5, "Small" starts at 6, and the last paragraph, empty, at 12.
constexpr int kEmpty = 5;
constexpr int kSmall = 6;
constexpr int kLast = 12;

void place(writeit::MainWindow& window, int offset)
{
  auto buffer = MainWindowProbe::buffer(window);
  buffer->place_cursor(buffer->get_iter_at_offset(offset));
  settle();
}

// Typed as a user types it: one user action, so the window formats it.
void type(writeit::MainWindow& window, const char* text)
{
  auto buffer = MainWindowProbe::buffer(window);
  buffer->begin_user_action();
  buffer->insert_interactive_at_cursor(text);
  buffer->end_user_action();
  settle();
}

std::string shown(writeit::MainWindow& window)
{
  return MainWindowProbe::size_combo(window).get_active_text().raw();
}

int line_height(writeit::MainWindow& window, int offset)
{
  int y = 0;
  int height = 0;
  MainWindowProbe::text(window).get_line_yrange(
      MainWindowProbe::buffer(window)->get_iter_at_offset(offset), y, height);
  return height;
}

bool contains(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

// A key press as the keyboard delivers it: to the window, which hands it to
// whatever has the keyboard focus, after its own accelerators if it lets them.
void key(writeit::MainWindow& window, guint keyval, GdkModifierType state = GdkModifierType(0))
{
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(window.get_window()->gobj()));
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
  gtk_main_do_event(event);
  gdk_event_free(event);
  settle();
}

void ctrl(writeit::MainWindow& window, guint keyval)
{
  key(window, keyval, GDK_CONTROL_MASK);
}

// Types `text` at the keyboard, one key per character.
void keys(writeit::MainWindow& window, const char* text)
{
  for (const char* c = text; *c; ++c)
    key(window, gdk_unicode_to_keyval(static_cast<guchar>(*c)));
}

// The box takes the keyboard as a click into it does: without selecting.
Gtk::Entry& click_into_box(writeit::MainWindow& window)
{
  Gtk::Entry& entry = *MainWindowProbe::size_combo(window).get_entry();
  entry.grab_focus_without_selecting();
  settle();
  return entry;
}

void select(writeit::MainWindow& window, int from, int to)
{
  auto buffer = MainWindowProbe::buffer(window);
  MainWindowProbe::text(window).grab_focus();
  buffer->select_range(buffer->get_iter_at_offset(from), buffer->get_iter_at_offset(to));
  settle();
}

std::pair<int, int> selection(writeit::MainWindow& window)
{
  Gtk::TextIter from;
  Gtk::TextIter to;
  MainWindowProbe::buffer(window)->get_selection_bounds(from, to);
  return {from.get_offset(), to.get_offset()};
}

// Every character from `from` to `to` is `size` pt.
bool sized(writeit::MainWindow& window, int from, int to, double size)
{
  for (int k = from; k < to; ++k) {
    if (MainWindowProbe::format_at(window, k).size != size)
      return false;
  }
  return true;
}

// The text of the box's active list item, or "" with none.
std::string highlighted(writeit::MainWindow& window)
{
  auto& combo = MainWindowProbe::size_combo(window);
  auto row = combo.get_active();
  if (!row)
    return "";
  Glib::ustring text;
  row->get_value(0, text);
  return text.raw();
}

// The row of the box's list that says `text`, or -1.
int row_of(writeit::MainWindow& window, const char* text)
{
  auto model = MainWindowProbe::size_combo(window).get_model();
  int row = 0;
  for (const auto& child : model->children()) {
    Glib::ustring label;
    child->get_value(0, label);
    if (label == text)
      return row;
    ++row;
  }
  return -1;
}

// Pops the box's list and says which item it highlights, by row, or -1 for
// none; then closes it.
int popup_highlight(writeit::MainWindow& window)
{
  auto& combo = MainWindowProbe::size_combo(window);
  combo.popup();
  settle();
  int row = -1;
  AtkObject* popup = gtk_combo_box_get_popup_accessible(GTK_COMBO_BOX(combo.gobj()));
  GtkWidget* menu = popup ? gtk_accessible_get_widget(GTK_ACCESSIBLE(popup)) : nullptr;
  if (menu && GTK_IS_MENU_SHELL(menu)) {
    GtkWidget* selected = gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(menu));
    GList* items = gtk_container_get_children(GTK_CONTAINER(menu));
    row = selected ? g_list_index(items, selected) : -1;
    g_list_free(items);
  } else {
    row = -2;
  }
  combo.popdown();
  settle();
  return row;
}

int pixel_width(Gtk::Entry& entry, const char* text)
{
  int width = 0;
  int height = 0;
  entry.create_pango_layout(text)->get_pixel_size(width, height);
  return width;
}

// How much of the box's text area shows: GTK lays the arrow button over
// the entry when the box is given less than both need.
int visible_text_width(writeit::MainWindow& window)
{
  auto& combo = MainWindowProbe::size_combo(window);
  Gtk::Entry& entry = *combo.get_entry();
  GdkRectangle area{};
  gtk_entry_get_text_area(entry.gobj(), &area);
  const int left = entry.get_allocation().get_x() + area.x;
  int right = left + area.width;
  // The arrow is an internal child of the box inside the combo.
  std::vector<GtkWidget*> found;
  std::vector<GtkWidget*> todo = {GTK_WIDGET(combo.gobj())};
  while (!todo.empty()) {
    GtkWidget* widget = todo.back();
    todo.pop_back();
    if (GTK_IS_TOGGLE_BUTTON(widget))
      found.push_back(widget);
    else if (GTK_IS_CONTAINER(widget))
      gtk_container_forall(
          GTK_CONTAINER(widget),
          [](GtkWidget* child, gpointer list) {
            static_cast<std::vector<GtkWidget*>*>(list)->push_back(child);
          },
          &todo);
  }
  for (GtkWidget* button : found) {
    GtkAllocation at;
    gtk_widget_get_allocation(button, &at);
    if (at.x > left)
      right = std::min(right, at.x);
  }
  return right - left;
}

// Answers the box's message (Word 97's modal message box) from inside its
// run(), and remembers what it said.
struct Messages {
  int count = 0;
  std::string last;
  sigc::connection poll;
  Messages()
  {
    poll = Glib::signal_timeout().connect(
        [this] {
          for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
            auto* message = dynamic_cast<Gtk::MessageDialog*>(top);
            if (message && message->get_visible()) {
              ++count;
              last = message->property_text().get_value();
              message->response(Gtk::RESPONSE_OK);
            }
          }
          return true;
        },
        20);
  }
  ~Messages()
  {
    poll.disconnect();
  }
};

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 48;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-font-size-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "font-size-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  Messages messages;
  // A message nobody answers would hang the suite: fail instead.
  Glib::signal_timeout().connect_seconds(
      [] {
        std::cerr << "font-size-ui: stuck in a dialog\n";
        std::exit(EXIT_FAILURE);
        return false;
      },
      50);
  const std::string path = Glib::build_filename(home, "sizes.rtf");
  Glib::file_set_contents(path, kFile);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    CHECK(MainWindowProbe::open(window, path));
    settle();
    auto buffer = MainWindowProbe::buffer(window);
    CHECK(buffer->get_text().raw() == "Half\n\nSmall\n");
    CHECK(!MainWindowProbe::dirty(window));

    // Open, then Save: the same bytes.
    CHECK(MainWindowProbe::save(window));
    CHECK(Glib::file_get_contents(path) == kFile);

    // The size box shows half points as Word 97 does.
    place(window, 2);
    CHECK(shown(window) == "10.5" && MainWindowProbe::format_at(window, 1).size == 10.5);
    place(window, kSmall + 2);
    CHECK(shown(window) == "8.5");

    // The empty line keeps its 20 pt: the box says so, it is drawn at 20 pt,
    // and typing there is 20 pt.
    place(window, kEmpty);
    CHECK(shown(window) == "20");
    CHECK(line_height(window, kEmpty) > line_height(window, 0));
    type(window, "x");
    CHECK(MainWindowProbe::format_at(window, kEmpty).size == 20);
    MainWindowProbe::undo(window);
    settle();
    CHECK(buffer->get_text().raw() == "Half\n\nSmall\n" && !MainWindowProbe::dirty(window));

    // The empty last paragraph too.
    place(window, kLast);
    CHECK(shown(window) == "20");
    type(window, "z");
    CHECK(MainWindowProbe::format_at(window, kLast).size == 20);
    MainWindowProbe::undo(window);
    settle();

    // Enter on the empty line makes another 20 pt empty line.
    place(window, kEmpty);
    type(window, "\n");
    {
      const writeit::Document doc = MainWindowProbe::capture(window);
      CHECK(doc.paragraphs.size() == 5 && doc.paragraphs[1].mark &&
            doc.paragraphs[1].mark->size == 20 && doc.paragraphs[2].mark &&
            doc.paragraphs[2].mark->size == 20);
    }
    MainWindowProbe::undo(window);
    settle();

    // Enter at the end of the 10.5 pt line: the new empty line is 10.5 pt.
    place(window, 4);
    type(window, "\n");
    {
      const writeit::Document doc = MainWindowProbe::capture(window);
      CHECK(doc.paragraphs.size() == 5 && doc.paragraphs[1].runs.empty() &&
            doc.paragraphs[1].mark && doc.paragraphs[1].mark->size == 10.5 &&
            doc.paragraphs[2].mark && doc.paragraphs[2].mark->size == 20);
    }
    MainWindowProbe::undo(window);
    settle();
    CHECK(buffer->get_text().raw() == "Half\n\nSmall\n" && !MainWindowProbe::dirty(window));

    // 10.5 typed into the box sizes the selection; 10.3 is refused.
    buffer->select_range(buffer->get_iter_at_offset(kSmall),
                         buffer->get_iter_at_offset(kSmall + 5));
    settle();
    auto* entry = MainWindowProbe::size_combo(window).get_entry();
    CHECK(entry != nullptr);
    // Typing alone changes nothing; Enter applies it.
    if (entry) {
      entry->set_text("10.3");
      settle();
      entry->activate();
      settle();
    }
    CHECK(MainWindowProbe::format_at(window, kSmall).size == 8.5 && shown(window) == "8.5");
    if (entry) {
      entry->set_text("10.5");
      settle();
    }
    CHECK(MainWindowProbe::format_at(window, kSmall).size == 8.5);
    if (entry) {
      entry->activate();
      settle();
    }
    CHECK(MainWindowProbe::format_at(window, kSmall).size == 10.5 && shown(window) == "10.5");
    // The list highlights the size applied, not its first item (8).
    CHECK(highlighted(window) == "10.5");
    CHECK(popup_highlight(window) == row_of(window, "10.5"));
    // A size typed that the list does not hold highlights nothing.
    if (entry) {
      entry->set_text("13");
      settle();
    }
    CHECK(popup_highlight(window) == -1);
    select(window, kSmall, kSmall + 5);

    // Saved as \fs21, the empty lines still \fs40.
    CHECK(MainWindowProbe::save(window));
    const std::string saved = Glib::file_get_contents(path);
    CHECK(contains(saved, "\\pard\\f0\\fs21\\b0\\i0\\ulnone Small\\par\n") &&
          contains(saved, "Half\\par\n\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n") &&
          contains(saved, "Small\\par\n\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n}"));

    // Bug Basher's review of #36. The box is wide enough to read "10.5" and
    // Word's largest size, "1638", in the font it uses: the part of its text
    // area that the arrow button does not cover holds either.
    {
      Gtk::Entry& entry = *MainWindowProbe::size_combo(window).get_entry();
      const int room = visible_text_width(window);
      if (room < pixel_width(entry, "1638"))
        std::cerr << "size box: " << room << " px of text area shows, \"10.5\" needs "
                  << pixel_width(entry, "10.5") << ", \"1638\" " << pixel_width(entry, "1638")
                  << "\n";
      CHECK(room >= pixel_width(entry, "10.5"));
      CHECK(room >= pixel_width(entry, "1638"));
    }

    // Editing keys typed in the box edit the box, not the document: the
    // window's Delete, Select All, Copy and Undo do not take them first.
    const std::string text = buffer->get_text().raw();
    const size_t steps = MainWindowProbe::undo_steps(window);
    select(window, kSmall, kSmall + 5);
    {
      click_into_box(window);
      ctrl(window, GDK_KEY_z);
      CHECK(sized(window, kSmall, kSmall + 5, 10.5) && MainWindowProbe::undo_steps(window) == steps &&
            buffer->get_text().raw() == text);
    }
    select(window, kSmall, kSmall + 5);
    {
      Gtk::Entry& entry = click_into_box(window);
      key(window, GDK_KEY_End);
      key(window, GDK_KEY_Home);
      key(window, GDK_KEY_Delete);
      CHECK(buffer->get_text().raw() == text && entry.get_text().raw() == "0.5");
      ctrl(window, GDK_KEY_a);
      int from = 0;
      int to = 0;
      CHECK(entry.get_selection_bounds(from, to) && from == 0 && to == 3);
      // And the document keeps its selection: the box selecting its own
      // text does not take it away.
      CHECK(selection(window) == std::make_pair(kSmall, kSmall + 5));
      ctrl(window, GDK_KEY_c);
      CHECK(Gtk::Clipboard::get()->wait_for_text().raw() == "0.5");
      key(window, GDK_KEY_BackSpace);
      CHECK(buffer->get_text().raw() == text && entry.get_text().raw().empty());
    }

    // Select the box's text with Shift+Home, type a size and press Enter:
    // it sizes the document's selection, as in Word.
    select(window, kSmall, kSmall + 5);
    {
      Gtk::Entry& entry = click_into_box(window);
      key(window, GDK_KEY_End);
      key(window, GDK_KEY_Home, GDK_SHIFT_MASK);
      CHECK(selection(window) == std::make_pair(kSmall, kSmall + 5));
      keys(window, "14");
      CHECK(entry.get_text().raw() == "14" && sized(window, kSmall, kSmall + 5, 10.5));
      key(window, GDK_KEY_Return);
      CHECK(sized(window, kSmall, kSmall + 5, 14) && sized(window, 0, 4, 10.5) &&
            selection(window) == std::make_pair(kSmall, kSmall + 5) && shown(window) == "14");
    }
    // Selected as a double-click selects it, likewise.
    select(window, kSmall, kSmall + 5);
    {
      Gtk::Entry& entry = click_into_box(window);
      entry.select_region(0, -1);
      settle();
      CHECK(selection(window) == std::make_pair(kSmall, kSmall + 5));
      keys(window, "12");
      key(window, GDK_KEY_Return);
      CHECK(sized(window, kSmall, kSmall + 5, 12) && sized(window, 0, 4, 10.5));
    }

    // A refused entry says why, in a message, as Word 97's box does. The
    // document is unchanged, and the box shows the size again and keeps the
    // keyboard.
    select(window, kSmall, kSmall + 5);
    {
      const writeit::Document before = MainWindowProbe::capture(window);
      const int seen = messages.count;
      Gtk::Entry& entry = click_into_box(window);
      entry.select_region(0, -1);
      keys(window, "10.3");
      key(window, GDK_KEY_Return);
      settle();
      CHECK(messages.count == seen + 1 &&
            messages.last == "Font sizes must be whole numbers or end in .5.");
      CHECK(MainWindowProbe::capture(window) == before && shown(window) == "12" &&
            entry.is_focus());
      entry.select_region(0, -1);
      keys(window, "abc");
      key(window, GDK_KEY_Return);
      settle();
      CHECK(messages.count == seen + 2 && messages.last == "This is not a valid number." &&
            MainWindowProbe::capture(window) == before && shown(window) == "12");
      // Spaces around a size are trimmed.
      entry.set_text("  16 ");
      key(window, GDK_KEY_Return);
      CHECK(messages.count == seen + 2 && sized(window, kSmall, kSmall + 5, 16));
    }

    // Bold, italic and underline on a selected empty line take, as size and
    // font do, and survive Save and reopening.
    select(window, kEmpty, kEmpty + 1);
    MainWindowProbe::bold_item(window)->activate();
    settle();
    select(window, kEmpty, kEmpty + 1);
    MainWindowProbe::italic_item(window)->activate();
    settle();
    select(window, kEmpty, kEmpty + 1);
    MainWindowProbe::underline_item(window)->activate();
    settle();
    {
      const writeit::Document doc = MainWindowProbe::capture(window);
      CHECK(doc.paragraphs.size() == 4 && doc.paragraphs[1].mark && doc.paragraphs[1].mark->bold &&
            doc.paragraphs[1].mark->italic && doc.paragraphs[1].mark->underline &&
            doc.paragraphs[1].mark->size == 20);
    }
    CHECK(MainWindowProbe::save(window));
    CHECK(contains(Glib::file_get_contents(path), "Half\\par\n\\pard\\f0\\fs40\\b\\i\\ul\\par\n"));
    CHECK(MainWindowProbe::open(window, path));
    settle();
    {
      const writeit::Document doc = MainWindowProbe::capture(window);
      CHECK(doc.paragraphs.size() == 4 && doc.paragraphs[1].mark && doc.paragraphs[1].mark->bold &&
            doc.paragraphs[1].mark->italic && doc.paragraphs[1].mark->underline);
    }
    // And off again.
    select(window, kEmpty, kEmpty + 1);
    MainWindowProbe::bold_item(window)->activate();
    settle();
    {
      const writeit::Document doc = MainWindowProbe::capture(window);
      CHECK(doc.paragraphs[1].mark && !doc.paragraphs[1].mark->bold &&
            doc.paragraphs[1].mark->italic);
    }

    window.hide();
    settle();
  }
  g_remove(path.c_str());
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("font-size-ui", kChecks);
}
