/* SPDX-License-Identifier: Unlicense */

// Table → Background paints the selected rows or columns with one cell
// shading, and None clears it. The file stores that as cell shading.

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

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static Gtk::TextView* cell_view(MainWindow& window, int row, int column)
  {
    return window.nth_cell_view(0, row, column);
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

Gtk::MenuItem* submenu_named(Gtk::MenuItem& parent, const char* label)
{
  Gtk::Menu* menu = parent.get_submenu();
  if (!menu)
    return nullptr;
  for (Gtk::Widget* child : menu->get_children()) {
    auto* item = dynamic_cast<Gtk::MenuItem*>(child);
    if (item && item->get_label() == label)
      return item;
  }
  return nullptr;
}

int colour_items(Gtk::MenuItem& parent)
{
  Gtk::Menu* menu = parent.get_submenu();
  if (!menu)
    return 0;
  int count = 0;
  for (Gtk::Widget* child : menu->get_children()) {
    if (dynamic_cast<Gtk::SeparatorMenuItem*>(child))
      continue;
    auto* item = dynamic_cast<Gtk::MenuItem*>(child);
    if (item && item->get_label() != "_None")
      ++count;
  }
  return count;
}

void click_view(Gtk::TextView& view, double x, double y, guint time, bool shift)
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
    event->button.button = 1;
    guint state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
    if (shift)
      state |= GDK_SHIFT_MASK;
    event->button.state = state;
    if (pointer) {
      event->button.device = pointer->gobj();
      gdk_event_set_device(event, pointer->gobj());
      gdk_event_set_source_device(event, pointer->gobj());
    }
    gtk_main_do_event(event);
    gdk_event_free(event);
  };
  send(GDK_BUTTON_PRESS);
  send(GDK_BUTTON_RELEASE);
  settle();
}

writeit::Document make_table(int rows, int columns)
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::insert_table(doc.paragraphs, 1, rows, columns, 4800);
  return doc;
}

void name_cells(writeit::Document& doc)
{
  const char* names[] = {"A", "B", "C", "D", "E", "F"};
  int n = 0;
  for (writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || n >= 6)
      continue;
    writeit::Run run;
    run.text = names[n++];
    paragraph.runs = {run};
  }
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

bool cell_fill(const writeit::Document& doc, int row, int column, int pattern, writeit::Ink fill)
{
  bool seen = false;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    seen = true;
    if (paragraph.cell.shading.pattern != pattern || paragraph.cell.shading.background != fill ||
        paragraph.cell.shading.foreground != writeit::Ink::Auto)
      return false;
  }
  return seen;
}

bool plain_cell(const writeit::Document& doc, int row, int column)
{
  return cell_fill(doc, row, column, 0, writeit::Ink::Auto);
}

writeit::Shading solid(writeit::Ink ink)
{
  writeit::Shading shading;
  shading.pattern = writeit::kMaxShading;
  shading.background = ink;
  return shading;
}

bool paint(writeit::Document& doc, writeit::TableBand band, int first, int last,
           const writeit::Shading& shading)
{
  return writeit::paint_table_band(doc.paragraphs, 1, band, first, last, shading);
}

}  // namespace

constexpr int kChecks = 42;

int main(int argc, char* argv[])
{
  using writeit::Ink;
  using writeit::TableBand;

  writeit::Document doc = make_table(3, 2);
  name_cells(doc);
  const writeit::Document untouched = doc;
  CHECK(!writeit::paint_table_band(doc.paragraphs, 0, TableBand::Rows, 0, 0, solid(Ink::Red)));
  CHECK(!paint(doc, TableBand::Rows, 3, 3, solid(Ink::Red)));
  CHECK(doc == untouched);
  CHECK(paint(doc, TableBand::Rows, 2, 0, solid(Ink::Red)));
  bool all_red = true;
  for (int row = 0; row < 3; ++row) {
    all_red = all_red && cell_fill(doc, row, 0, 10000, Ink::Red);
    all_red = all_red && cell_fill(doc, row, 1, 10000, Ink::Red);
  }
  CHECK(all_red);
  CHECK(cell_text(doc, 0, 0) == "A" && cell_text(doc, 0, 1) == "B" && cell_text(doc, 1, 0) == "C" &&
        cell_text(doc, 1, 1) == "D" && cell_text(doc, 2, 0) == "E" && cell_text(doc, 2, 1) == "F");
  bool frame_kept = true;
  bool paragraph_plain = true;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0)
      continue;
    frame_kept = frame_kept && !paragraph.cell.borders_set;
    paragraph_plain = paragraph_plain && writeit::shading_plain(paragraph.shading);
  }
  CHECK(frame_kept);
  CHECK(paragraph_plain);

  writeit::Paragraph extra = doc.paragraphs[3];
  extra.runs[0].text = "more";
  doc.paragraphs.insert(doc.paragraphs.begin() + 4, extra);
  doc.paragraphs[3].cell.borders_set = true;
  doc.paragraphs[3].cell.borders.top.style = writeit::BorderStyle::Single;
  doc.paragraphs[3].cell.borders.top.width = 10;
  doc.paragraphs[4].cell.borders_set = true;
  doc.paragraphs[4].cell.borders.top.style = writeit::BorderStyle::Single;
  doc.paragraphs[4].cell.borders.top.width = 10;
  doc.paragraphs[3].shading.pattern = 5000;
  doc.paragraphs[3].shading.background = Ink::Green;
  CHECK(paint(doc, TableBand::Rows, 1, 1, solid(Ink::Blue)));
  CHECK(cell_fill(doc, 1, 0, 10000, Ink::Blue));
  CHECK(cell_fill(doc, 1, 1, 10000, Ink::Blue));
  CHECK(cell_fill(doc, 0, 0, 10000, Ink::Red) && cell_fill(doc, 2, 1, 10000, Ink::Red));
  CHECK(doc.paragraphs[3].cell.borders_set &&
        doc.paragraphs[3].cell.borders.top.style == writeit::BorderStyle::Single);
  CHECK(doc.paragraphs[3].shading.pattern == 5000 &&
        doc.paragraphs[3].shading.background == Ink::Green);
  CHECK(cell_text(doc, 1, 0) == "Cmore");

  CHECK(paint(doc, TableBand::Rows, 1, 1, writeit::Shading{}));
  CHECK(plain_cell(doc, 1, 0) && plain_cell(doc, 1, 1));
  CHECK(cell_fill(doc, 0, 0, 10000, Ink::Red));

  CHECK(paint(doc, TableBand::Columns, 1, 1, solid(Ink::Yellow)));
  CHECK(cell_fill(doc, 0, 1, 10000, Ink::Yellow) && cell_fill(doc, 1, 1, 10000, Ink::Yellow) &&
        cell_fill(doc, 2, 1, 10000, Ink::Yellow));
  CHECK(cell_fill(doc, 0, 0, 10000, Ink::Red) && plain_cell(doc, 1, 0) &&
        cell_fill(doc, 2, 0, 10000, Ink::Red));

  writeit::Document again;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), again));
  CHECK(cell_fill(again, 0, 0, 10000, Ink::Red) && cell_fill(again, 1, 1, 10000, Ink::Yellow) &&
        plain_cell(again, 1, 0));

  for (writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.row == 0 && paragraph.cell.column == 1) {
      paragraph.cell.shading.pattern = 5000;
      paragraph.cell.shading.foreground = Ink::Black;
      paragraph.cell.shading.background = Ink::Red;
    }
  }
  CHECK(cell_fill(doc, 0, 0, 10000, Ink::Red) && cell_fill(doc, 2, 1, 10000, Ink::Yellow));
  writeit::Document mixed;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), mixed));
  bool percent = false;
  for (const writeit::Paragraph& paragraph : mixed.paragraphs) {
    if (paragraph.cell.row != 0 || paragraph.cell.column != 1)
      continue;
    percent = paragraph.cell.shading.pattern == 5000 &&
              paragraph.cell.shading.foreground == Ink::Black &&
              paragraph.cell.shading.background == Ink::Red;
  }
  CHECK(percent);
  CHECK(cell_fill(mixed, 2, 1, 10000, Ink::Yellow));

  writeit::Document both = make_table(1, 1);
  writeit::insert_table(both.paragraphs, both.paragraphs.size(), 1, 1, 4800);
  const int second = both.paragraphs.back().cell.table;
  CHECK(writeit::paint_table_band(both.paragraphs, 1, TableBand::Rows, 0, 0, solid(Ink::Red)));
  bool second_plain = true;
  for (const writeit::Paragraph& paragraph : both.paragraphs) {
    if (paragraph.cell.table != second)
      continue;
    second_plain = second_plain && writeit::shading_plain(paragraph.cell.shading);
  }
  CHECK(second_plain);

  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-row-colour-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "row-colour: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.resize(800, 600);
    window.show();
    settle();
    Gtk::MenuItem* background = menu_named(window, "T_able", "_Background");
    CHECK(background);
    CHECK(background && !background->get_sensitive());

    writeit::MainWindowProbe::load(window, make_table(3, 2));
    settle();
    auto cell = [&](int row, int column) {
      return writeit::MainWindowProbe::cell_view(window, row, column);
    };
    Gtk::TextView* row_cell = cell(0, 0);
    CHECK(row_cell);
    if (row_cell)
      click_view(*row_cell, 2, 10, 1000, false);
    CHECK(background && background->get_sensitive());
    Gtk::MenuItem* red = background ? submenu_named(*background, "Red") : nullptr;
    Gtk::MenuItem* none = background ? submenu_named(*background, "_None") : nullptr;
    CHECK(red && none);
    CHECK(background && colour_items(*background) == 16);
    if (red)
      red->activate();
    settle();
    writeit::Document painted = writeit::MainWindowProbe::doc(window);
    CHECK(cell_fill(painted, 0, 0, 10000, Ink::Red) && cell_fill(painted, 0, 1, 10000, Ink::Red));
    CHECK(plain_cell(painted, 1, 0) && plain_cell(painted, 2, 1));

    writeit::MainWindowProbe::undo(window);
    settle();
    writeit::Document restored = writeit::MainWindowProbe::doc(window);
    CHECK(plain_cell(restored, 0, 0) && plain_cell(restored, 0, 1) && plain_cell(restored, 2, 0));

    row_cell = cell(0, 0);
    if (row_cell)
      click_view(*row_cell, 20, 2, 2000, false);
    Gtk::MenuItem* blue = background ? submenu_named(*background, "Blue") : nullptr;
    if (blue)
      blue->activate();
    settle();
    writeit::Document column = writeit::MainWindowProbe::doc(window);
    CHECK(cell_fill(column, 0, 0, 10000, Ink::Blue) && cell_fill(column, 2, 0, 10000, Ink::Blue));
    CHECK(plain_cell(column, 0, 1) && plain_cell(column, 1, 1));

    row_cell = cell(0, 0);
    if (row_cell)
      click_view(*row_cell, 20, 2, 3000, false);
    if (none)
      none->activate();
    settle();
    writeit::Document cleared = writeit::MainWindowProbe::doc(window);
    CHECK(plain_cell(cleared, 0, 0) && plain_cell(cleared, 2, 0) && plain_cell(cleared, 0, 1));

    row_cell = cell(1, 0);
    if (row_cell)
      click_view(*row_cell, 30, 20, 4000, false);
    CHECK(background && !background->get_sensitive());
  }
  g_rmdir(home.c_str());
  return suite_test::done("row-colour", kChecks);
}
