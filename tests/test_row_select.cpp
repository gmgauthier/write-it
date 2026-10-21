/* SPDX-License-Identifier: Unlicense */

// Rows and columns in the real window. The left edge of the first column
// selects that row, and the top edge of the first row selects that column.
// Shift extends one kind. Copy and Paste then insert the band after the
// caret's row or column. In ordinary text the cells become paragraphs at
// the caret.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace writeit {

struct MainWindowProbe {
  static void load(MainWindow& window, const Document& doc)
  {
    window.undo_.clear();
    window.replace_buffer(doc, 0);
    window.saved_state_ = window.undo_.state_id();
    window.save_point_ = true;
    window.last_typed_us_ = 0;
    window.update_title();
    window.update_actions();
    window.sync_format_controls();
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }

  static std::string message(MainWindow& window)
  {
    return window.message_.get_text();
  }

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static void caret(MainWindow& window, int offset)
  {
    auto buffer = window.buffer_;
    const int count = buffer->get_char_count();
    const int at = offset < 0 ? 0 : offset > count ? count : offset;
    buffer->place_cursor(buffer->get_iter_at_offset(at));
  }

  static Gtk::TextView* cell_view(MainWindow& window, int row, int column)
  {
    return window.nth_cell_view(0, row, column);
  }

  static bool copied_rows(MainWindow& window)
  {
    return window.table_copy_on_ && window.table_copy_.band == TableBand::Rows;
  }

  static void focus_cell(MainWindow& window, int row, int column)
  {
    Gtk::TextView* view = cell_view(window, row, column);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->get_char_count() > 0 ? buffer->end() : buffer->begin());
  }

  static void click_body(MainWindow& window)
  {
    auto text_win = window.text_.get_window(Gtk::TEXT_WINDOW_TEXT);
    if (!text_win) {
      window.text_.grab_focus();
      return;
    }
    int origin_x = 0;
    int origin_y = 0;
    text_win->get_origin(origin_x, origin_y);
    auto pointer = text_win->get_display()->get_default_seat()->get_pointer();
    auto send = [&](GdkEventType type) {
      GdkEvent* event = gdk_event_new(type);
      event->button.window = GDK_WINDOW(g_object_ref(text_win->gobj()));
      event->button.send_event = TRUE;
      event->button.time = 8000;
      event->button.x = 8;
      event->button.y = 8;
      event->button.x_root = origin_x + 8;
      event->button.y_root = origin_y + 8;
      event->button.button = 1;
      event->button.state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
      if (pointer) {
        gdk_event_set_device(event, pointer->gobj());
        gdk_event_set_source_device(event, pointer->gobj());
      }
      gtk_main_do_event(event);
      gdk_event_free(event);
    };
    send(GDK_BUTTON_PRESS);
    send(GDK_BUTTON_RELEASE);
  }
};

}  // namespace writeit

namespace {

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

void walk(Gtk::Widget& widget, const std::function<void(Gtk::Widget&)>& visit)
{
  visit(widget);
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    for (Gtk::Widget* child : container->get_children())
      walk(*child, visit);
  }
}

Gtk::MenuItem* menu_named(Gtk::Window& window, const char* menu, const char* label)
{
  Gtk::MenuItem* found = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    if (found)
      return;
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar)
      return;
    for (Gtk::Widget* child : bar->get_children()) {
      auto* top = dynamic_cast<Gtk::MenuItem*>(child);
      if (!top || top->get_label() != menu || !top->get_submenu())
        continue;
      for (Gtk::Widget* entry : top->get_submenu()->get_children()) {
        auto* item = dynamic_cast<Gtk::MenuItem*>(entry);
        if (item && item->get_label() == label)
          found = item;
      }
    }
  });
  return found;
}

void activate(Gtk::MenuItem* item)
{
  if (item)
    item->activate();
  settle();
}

void click_view(Gtk::TextView& view, double x, double y, guint time, bool shift, guint button = 1,
               bool release = true)
{
  auto text_win = view.get_window(Gtk::TEXT_WINDOW_TEXT);
  if (!text_win)
    return;
  int origin_x = 0;
  int origin_y = 0;
  text_win->get_origin(origin_x, origin_y);
  auto pointer = text_win->get_display()->get_default_seat()->get_pointer();
  auto send = [&](GdkEventType type) {
    GdkEvent* event = gdk_event_new(type);
    event->button.window = GDK_WINDOW(g_object_ref(text_win->gobj()));
    event->button.send_event = TRUE;
    event->button.time = time;
    event->button.x = x;
    event->button.y = y;
    event->button.x_root = origin_x + x;
    event->button.y_root = origin_y + y;
    event->button.button = button;
    guint state = 0;
    if (type == GDK_BUTTON_RELEASE)
      state = button == 3 ? GDK_BUTTON3_MASK : GDK_BUTTON1_MASK;
    if (shift)
      state |= GDK_SHIFT_MASK;
    event->button.state = state;
    if (pointer) {
      event->button.device = pointer->gobj();
      gdk_event_set_device(event, pointer->gobj());
      gdk_event_set_source_device(event, pointer->gobj());
    }
    // Button 1 goes through the normal path, which is what selects a band.
    // A right-click is delivered to the text view: GTK claims it before the
    // button-press signal and opens that view's own menu.
    if (button == 1)
      gtk_main_do_event(event);
    else
      gtk_widget_event(GTK_WIDGET(view.gobj()), event);
    gdk_event_free(event);
  };
  send(GDK_BUTTON_PRESS);
  if (release)
    send(GDK_BUTTON_RELEASE);
  settle();
}

// The cell's menu is attached to the text view, and it appears only after
// the clipboard answers.
Gtk::Menu* cell_popup(Gtk::TextView& view)
{
  // GTK owns this list. Freeing it detaches the menu from a dangling node.
  GList* list = gtk_menu_get_for_attach_widget(GTK_WIDGET(view.gobj()));
  Gtk::Menu* found = nullptr;
  for (GList* item = list; item; item = item->next) {
    auto* menu = Glib::wrap(GTK_MENU(item->data));
    if (menu && menu->get_mapped())
      found = menu;
  }
  return found;
}

Gtk::Menu* wait_cell_popup(Gtk::TextView& view)
{
  auto context = Glib::MainContext::get_default();
  for (int i = 0; i < 40; ++i) {
    while (context->pending())
      context->iteration(false);
    if (Gtk::Menu* menu = cell_popup(view))
      return menu;
    g_usleep(10000);
  }
  return nullptr;
}

Gtk::MenuItem* menu_item(Gtk::Menu& menu, const char* label)
{
  for (Gtk::Widget* child : menu.get_children()) {
    auto* item = dynamic_cast<Gtk::MenuItem*>(child);
    if (item && (item->get_label() == label || item->get_label() == "Copy"))
      return item;
  }
  return nullptr;
}

bool banded(Gtk::TextView* view)
{
  auto style = view ? view->get_style_context() : Glib::RefPtr<Gtk::StyleContext>();
  return style && style->has_class("cell-band");
}

bool shaded(Gtk::TextView* view)
{
  auto style = view ? view->get_style_context() : Glib::RefPtr<Gtk::StyleContext>();
  return style && style->has_class("cell-shade");
}

std::string cell_text(const writeit::Document& doc, int row, int column)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
  }
  return out;
}

int table_rows(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      return paragraph.cell.rows;
  }
  return 0;
}

int table_columns(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      return paragraph.cell.columns;
  }
  return 0;
}

bool cell_shaded(const writeit::Document& doc, int row, int column)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    return paragraph.cell.shading.pattern == 10000 &&
           paragraph.cell.shading.background == writeit::Ink::Yellow && paragraph.cell.borders_set;
  }
  return false;
}

std::string body_text(const writeit::Document& doc)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      continue;
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
    out.push_back('\n');
  }
  return out;
}

writeit::Document grid()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::Run intro;
  intro.text = "Intro";
  doc.paragraphs[0].runs.push_back(intro);
  writeit::insert_table(doc.paragraphs, 1, 3, 2, writeit::page_text_twips(doc.page));
  const char* names[] = {"A", "B", "C", "D", "E", "F"};
  for (int i = 0; i < 6; ++i) {
    writeit::Run run;
    run.text = names[i];
    doc.paragraphs[static_cast<size_t>(1 + i)].runs.push_back(run);
  }
  writeit::Paragraph& painted = doc.paragraphs[3];
  painted.cell.shading.pattern = 10000;
  painted.cell.shading.background = writeit::Ink::Yellow;
  painted.cell.borders_set = true;
  painted.cell.borders.top.style = writeit::BorderStyle::Single;
  painted.cell.borders.top.width = 10;
  return doc;
}

writeit::Document plain_table(int rows, int columns)
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::insert_table(doc.paragraphs, 1, rows, columns, writeit::page_text_twips(doc.page));
  return doc;
}

}  // namespace

constexpr int kChecks = 59;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-rows-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "row-select: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.resize(800, 600);
    window.show();
    settle();
    Gtk::MenuItem* copy = menu_named(window, "_Edit", "_Copy");
    Gtk::MenuItem* paste = menu_named(window, "_Edit", "_Paste");
    CHECK(copy && paste);

    writeit::MainWindowProbe::load(window, grid());
    settle();
    auto cell = [&](int row, int column) {
      return writeit::MainWindowProbe::cell_view(window, row, column);
    };
    CHECK(copy && !copy->get_sensitive());

    Gtk::TextView* row_cell = cell(1, 0);
    CHECK(row_cell);
    if (row_cell)
      click_view(*row_cell, 2, 10, 1000, false);
    CHECK(banded(cell(1, 0)) && banded(cell(1, 1)));
    CHECK(!banded(cell(0, 0)) && !banded(cell(0, 1)));
    CHECK(!banded(cell(2, 0)) && !banded(cell(2, 1)));
    CHECK(shaded(cell(1, 0)) && banded(cell(1, 0)));
    CHECK(copy && copy->get_sensitive());

    Gtk::TextView* extend = cell(2, 0);
    if (extend)
      click_view(*extend, 2, 10, 2000, true);
    CHECK(banded(cell(1, 0)) && banded(cell(1, 1)));
    CHECK(banded(cell(2, 0)) && banded(cell(2, 1)));
    CHECK(!banded(cell(0, 0)) && !banded(cell(0, 1)));

    Gtk::TextView* top = cell(0, 0);
    if (top)
      click_view(*top, 20, 2, 3000, true);
    CHECK(banded(cell(0, 0)) && banded(cell(1, 0)) &&
          banded(cell(2, 0)));
    CHECK(!banded(cell(0, 1)) && !banded(cell(1, 1)) &&
          !banded(cell(2, 1)));

    Gtk::TextView* inside = cell(1, 0);
    if (inside)
      click_view(*inside, 30, 20, 4000, false);
    CHECK(!banded(cell(1, 0)) && !banded(cell(1, 1)));
    CHECK(!banded(cell(0, 0)) && !banded(cell(2, 0)));
    CHECK(copy && !copy->get_sensitive());

    if (row_cell)
      click_view(*row_cell, 2, 10, 5000, false);
    CHECK(banded(cell(1, 0)) && banded(cell(1, 1)));
    CHECK(!banded(cell(0, 0)) && !banded(cell(2, 0)));
    if (row_cell)
      click_view(*row_cell, 30, 20, 5500, false, 3, false);
    Gtk::Menu* popup = row_cell ? wait_cell_popup(*row_cell) : nullptr;
    CHECK(popup);
    CHECK(banded(cell(1, 0)) && banded(cell(1, 1)));
    Gtk::MenuItem* cell_copy = popup ? menu_item(*popup, "_Copy") : nullptr;
    CHECK(cell_copy && cell_copy->get_sensitive());
    activate(cell_copy);
    if (popup)
      popup->popdown();
    settle();
    CHECK(writeit::MainWindowProbe::copied_rows(window));
    writeit::MainWindowProbe::focus_cell(window, 0, 0);
    settle();
    CHECK(paste && paste->get_sensitive());
    activate(paste);
    writeit::Document pasted = writeit::MainWindowProbe::doc(window);
    CHECK(table_rows(pasted) == 4 && table_columns(pasted) == 2);
    CHECK(cell_text(pasted, 0, 0) == "A" && cell_text(pasted, 0, 1) == "B");
    CHECK(cell_text(pasted, 1, 0) == "C" && cell_text(pasted, 1, 1) == "D");
    CHECK(cell_text(pasted, 2, 0) == "C" && cell_text(pasted, 2, 1) == "D");
    CHECK(cell_text(pasted, 3, 0) == "E" && cell_text(pasted, 3, 1) == "F");
    CHECK(cell_shaded(pasted, 1, 0) && cell_shaded(pasted, 2, 0));
    CHECK(!cell_shaded(pasted, 0, 0) && !cell_shaded(pasted, 3, 0));
    writeit::MainWindowProbe::undo(window);
    settle();
    writeit::Document restored = writeit::MainWindowProbe::doc(window);
    CHECK(table_rows(restored) == 3);
    CHECK(cell_text(restored, 0, 0) == "A" && cell_text(restored, 1, 0) == "C" &&
          cell_text(restored, 2, 0) == "E");
    CHECK(cell_shaded(restored, 1, 0) && !cell_shaded(restored, 2, 0));

    writeit::MainWindowProbe::click_body(window);
    settle();
    writeit::MainWindowProbe::caret(window, 0);
    activate(paste);
    writeit::Document as_text = writeit::MainWindowProbe::doc(window);
    CHECK(body_text(as_text) == "C\nD\nIntro\n");
    CHECK(table_rows(as_text) == 3 && cell_text(as_text, 0, 0) == "A" &&
          cell_text(as_text, 1, 0) == "C");
    CHECK(as_text.paragraphs[0].cell.table == 0 && as_text.paragraphs[0].cell.shading.pattern == 0);
    writeit::MainWindowProbe::undo(window);
    settle();
    writeit::Document text_back = writeit::MainWindowProbe::doc(window);
    CHECK(body_text(text_back) == "Intro\n" && table_rows(text_back) == 3);

    writeit::MainWindowProbe::caret(window, 2);
    activate(paste);
    writeit::Document at_mid = writeit::MainWindowProbe::doc(window);
    CHECK(body_text(at_mid) == "In\nC\nD\ntro\n");
    CHECK(table_rows(at_mid) == 3 && cell_text(at_mid, 0, 0) == "A");

    writeit::MainWindowProbe::load(window, writeit::blank_document("Sans", 11));
    settle();
    activate(paste);
    writeit::Document replaced = writeit::MainWindowProbe::doc(window);
    CHECK(replaced.paragraphs.size() == 2 && body_text(replaced) == "C\nD\n");
    CHECK(table_rows(replaced) == 0);
    writeit::MainWindowProbe::undo(window);
    settle();
    writeit::Document blank_again = writeit::MainWindowProbe::doc(window);
    CHECK(blank_again.paragraphs.size() == 1 && body_text(blank_again) == "\n" &&
          table_rows(blank_again) == 0);

    writeit::MainWindowProbe::load(window, plain_table(2, 3));
    settle();
    writeit::MainWindowProbe::focus_cell(window, 0, 0);
    settle();
    activate(paste);
    CHECK(writeit::MainWindowProbe::message(window) == writeit::kTablePasteMisfit);
    writeit::Document wide = writeit::MainWindowProbe::doc(window);
    CHECK(table_rows(wide) == 2 && table_columns(wide) == 3);
    CHECK(cell_text(wide, 0, 0).empty() && cell_text(wide, 1, 2).empty());

    writeit::MainWindowProbe::load(window, grid());
    settle();
    Gtk::TextView* column_cell = cell(0, 0);
    if (column_cell)
      click_view(*column_cell, 20, 2, 6000, false);
    CHECK(banded(cell(0, 0)) && banded(cell(2, 0)));
    CHECK(!banded(cell(0, 1)));
    activate(copy);
    writeit::MainWindowProbe::focus_cell(window, 0, 0);
    settle();
    activate(paste);
    writeit::Document columns = writeit::MainWindowProbe::doc(window);
    CHECK(table_columns(columns) == 3 && table_rows(columns) == 3);
    CHECK(cell_text(columns, 0, 0) == "A" && cell_text(columns, 0, 1) == "A" &&
          cell_text(columns, 0, 2) == "B");
    CHECK(cell_text(columns, 1, 0) == "C" && cell_text(columns, 1, 1) == "C" &&
          cell_text(columns, 1, 2) == "D");
    CHECK(cell_text(columns, 2, 0) == "E" && cell_text(columns, 2, 1) == "E" &&
          cell_text(columns, 2, 2) == "F");
    CHECK(cell_shaded(columns, 1, 0) && cell_shaded(columns, 1, 1));
    CHECK(writeit::MainWindowProbe::message(window).empty());
    writeit::MainWindowProbe::undo(window);
    settle();
    CHECK(table_columns(writeit::MainWindowProbe::doc(window)) == 2);

    writeit::MainWindowProbe::load(window, plain_table(32, 1));
    settle();
    Gtk::TextView* full = cell(0, 0);
    CHECK(full);
    if (full)
      click_view(*full, 2, 10, 7000, false);
    CHECK(banded(cell(0, 0)));
    CHECK(copy && copy->get_sensitive());
    activate(copy);
    writeit::MainWindowProbe::focus_cell(window, 0, 0);
    settle();
    activate(paste);
    CHECK(writeit::MainWindowProbe::message(window) == writeit::kTablePasteLimit);
    CHECK(table_rows(writeit::MainWindowProbe::doc(window)) == 32);
  }
  return suite_test::done("row-select", kChecks);
}
