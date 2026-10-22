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
#include <iostream>
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

  // The note column sits on the marker's sheet, just above that sheet's foot.
  // A place that ran before the seam tags were in the line tops is short by
  // those seams, up in the page above.
  static bool notes_at_seam(MainWindow& window)
  {
    auto fail = [](const std::string& why) {
      std::cerr << "notes: " << why << "\n";
      return false;
    };
    if (!window.buffer_ || !window.note_box_.get_visible() || window.note_groups_.size() > 1)
      return fail("column not showing");
    int marker_y = -1;
    for (auto iter = window.buffer_->begin(); !iter.is_end(); ++iter) {
      bool marked = false;
      for (const auto& tag : iter.get_tags()) {
        if (tag->property_rise().get_value() > 0)
          marked = true;
      }
      if (!marked)
        continue;
      int top = 0;
      int height = 0;
      window.text_.get_line_yrange(iter, top, height);
      marker_y = top;
      break;
    }
    if (marker_y < 0)
      return fail("no marker");
    int seam_top = -1;
    int seam = 0;
    auto iter = window.buffer_->begin();
    while (!iter.is_end()) {
      int pixels = 0;
      for (const auto& tag : iter.get_tags())
        pixels = std::max(pixels, page_seam_pixels(tag->property_name().get_value().raw()));
      if (pixels > 0) {
        int top = 0;
        int height = 0;
        window.text_.get_line_yrange(iter, top, height);
        if (top > marker_y) {
          seam_top = top;
          seam = pixels;
          break;
        }
      }
      bool more = false;
      while (!iter.is_end()) {
        if (iter.get_char() == '\n') {
          iter.forward_char();
          more = !iter.is_end();
          break;
        }
        iter.forward_char();
      }
      if (!more)
        break;
    }
    if (seam_top < 0)
      return fail("no seam below the marker");
    const ViewGeometry sheet = window.geometry();
    const int chrome = sheet.margin_bottom + sheet.gap + sheet.margin_y;
    const int buffer_y = seam_top + seam - chrome - window.note_h_;
    int wx = 0;
    int wy = 0;
    window.text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, buffer_y, wx, wy);
    int ox = 0;
    int oy = 0;
    if (!window.text_.translate_coordinates(window.page_overlay_, wx, wy, ox, oy))
      return fail("no overlay");
    const int miss = oy > window.note_y_ ? oy - window.note_y_ : window.note_y_ - oy;
    if (miss > 2)
      return fail("note_y " + std::to_string(window.note_y_) + " seam foot " + std::to_string(oy) +
                  " marker " + std::to_string(marker_y));
    return true;
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

// A pixel of the cell's text window. The frame's own fill sits behind that
// window, so this is the colour the page shows.
bool near_color(Gtk::TextView* view, int x, int y, int er, int eg, int eb)
{
  int r = -1;
  int g = -1;
  int b = -1;
  auto fail = [&]() {
    std::cerr << "cell pixel " << x << "," << y << " is " << r << " " << g << " " << b
              << ", expected " << er << " " << eg << " " << eb << "\n";
    return false;
  };
  if (!view)
    return fail();
  auto win = view->get_window(Gtk::TEXT_WINDOW_TEXT);
  if (!win || x < 0 || y < 0 || x >= win->get_width() || y >= win->get_height())
    return fail();
  try {
    auto pix = Gdk::Pixbuf::create(win, x, y, 1, 1);
    if (!pix || !pix->get_pixels() || pix->get_n_channels() < 3)
      return fail();
    const guint8* p = pix->get_pixels();
    r = p[0];
    g = p[1];
    b = p[2];
  } catch (const Glib::Error&) {
    return fail();
  }
  const int dr = r > er ? r - er : er - r;
  const int dg = g > eg ? g - eg : eg - g;
  const int db = b > eb ? b - eb : eb - b;
  if (dr > 3 || dg > 3 || db > 3)
    return fail();
  return true;
}

}  // namespace

writeit::Document footnote_before_table()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  auto line = [](const std::string& text) {
    writeit::Paragraph paragraph;
    writeit::Run run;
    run.text = text;
    paragraph.runs.push_back(std::move(run));
    return paragraph;
  };
  for (int i = 0; i < 8; ++i)
    doc.paragraphs.push_back(
        line("Body line " + std::to_string(i) + " fills the page before the note."));
  writeit::Paragraph noted = line("The footnote marker sits on this paragraph. ");
  noted.page_break = true;
  writeit::Run marker;
  marker.text = "1";
  marker.note = 1;
  noted.runs.push_back(marker);
  doc.paragraphs.push_back(std::move(noted));
  for (int i = 0; i < 4; ++i)
    doc.paragraphs.push_back(line("Still on the note's page, line " + std::to_string(i)));
  writeit::Paragraph heading = line("Sample Table");
  heading.page_break = true;
  doc.paragraphs.push_back(std::move(heading));
  writeit::insert_table(doc.paragraphs, doc.paragraphs.size(), 2, 2, 4800);
  for (writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0)
      continue;
    writeit::Run run;
    run.text = "cell";
    paragraph.runs = {run};
  }
  writeit::Paragraph note;
  writeit::Run note_run;
  note_run.text = "buster keaton";
  note.runs.push_back(note_run);
  doc.notes.push_back({note});
  return doc;
}

constexpr int kChecks = 56;

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

    auto cell = [&](int row, int column) {
      return writeit::MainWindowProbe::cell_view(window, row, column);
    };
    writeit::Document preset = make_table(2, 2);
    for (writeit::Paragraph& paragraph : preset.paragraphs) {
      if (paragraph.cell.table != 0 && paragraph.cell.row == 0)
        paragraph.cell.shading = solid(Ink::Red);
    }
    writeit::MainWindowProbe::load(window, preset);
    settle();
    CHECK(near_color(cell(0, 0), 2, 1, 255, 0, 0));
    CHECK(near_color(cell(1, 0), 2, 1, 255, 255, 255));

    writeit::MainWindowProbe::load(window, make_table(3, 2));
    settle();
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
    // The command rebuilds the sheet, so the band is gone and the cell is the
    // colour itself. Selecting that row again tints it, and must not cover it.
    CHECK(near_color(cell(0, 0), 2, 1, 255, 0, 0));
    row_cell = cell(0, 0);
    if (row_cell)
      click_view(*row_cell, 2, 10, 1400, false);
    CHECK(near_color(cell(0, 0), 2, 1, 192, 71, 99));
    {
      Gtk::TextView* away = cell(1, 0);
      if (away)
        click_view(*away, 30, 20, 1500, false);
    }
    CHECK(near_color(cell(0, 0), 2, 1, 255, 0, 0));

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
    {
      Gtk::TextView* away = cell(0, 1);
      if (away)
        click_view(*away, 30, 20, 2500, false);
    }
    CHECK(near_color(cell(2, 0), 2, 1, 0, 0, 255));

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
    CHECK(near_color(cell(0, 0), 2, 1, 255, 255, 255));

    writeit::Document layered = make_table(1, 1);
    for (writeit::Paragraph& paragraph : layered.paragraphs) {
      if (paragraph.cell.table == 0)
        continue;
      paragraph.cell.shading = solid(Ink::Red);
      paragraph.shading.pattern = writeit::kMaxShading;
      paragraph.shading.background = Ink::Green;
      writeit::Run run;
      run.text = "Word";
      paragraph.runs = {run};
    }
    writeit::MainWindowProbe::load(window, layered);
    settle();
    // The cell colour fills the padding. The paragraph's own shading covers the line.
    CHECK(near_color(cell(0, 0), 2, 1, 255, 0, 0));
    CHECK(near_color(cell(0, 0), 70, 8, 0, 255, 0));

    // The first Table → Background rebuilds the sheet. The notes above that
    // table stay on the marker's page, at its foot.
    writeit::MainWindowProbe::load(window, footnote_before_table());
    settle();
    CHECK(writeit::MainWindowProbe::notes_at_seam(window));
    Gtk::TextView* noted_cell = cell(0, 0);
    if (noted_cell)
      click_view(*noted_cell, 2, 10, 5000, false);
    Gtk::MenuItem* yellow = background ? submenu_named(*background, "Yellow") : nullptr;
    CHECK(yellow);
    if (yellow)
      yellow->activate();
    settle();
    CHECK(cell_fill(writeit::MainWindowProbe::doc(window), 0, 0, 10000, Ink::Yellow));
    CHECK(writeit::MainWindowProbe::notes_at_seam(window));
    noted_cell = cell(0, 0);
    if (noted_cell)
      click_view(*noted_cell, 2, 10, 6000, false);
    Gtk::MenuItem* again = background ? submenu_named(*background, "Red") : nullptr;
    if (again)
      again->activate();
    settle();
    CHECK(writeit::MainWindowProbe::notes_at_seam(window));
  }
  g_rmdir(home.c_str());
  return suite_test::done("row-colour", kChecks);
}
