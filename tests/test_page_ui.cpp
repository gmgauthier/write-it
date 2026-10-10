/* SPDX-License-Identifier: Unlicense */

// Page commands in the real window, under a display (CI runs this in
// xvfb-run). Menus that open a dialog are not activated: a modal dialog
// would stall the suite. Page Break, Insert Row, and Footnote do not.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
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

std::vector<Gtk::MenuItem*> menu_items(Gtk::Window& window, const char* menu)
{
  std::vector<Gtk::MenuItem*> found;
  walk(window, [&](Gtk::Widget& widget) {
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar)
      return;
    for (Gtk::Widget* child : bar->get_children()) {
      auto* top = dynamic_cast<Gtk::MenuItem*>(child);
      if (!top || top->get_label() != menu || !top->get_submenu())
        continue;
      for (Gtk::Widget* entry : top->get_submenu()->get_children()) {
        if (auto* item = dynamic_cast<Gtk::MenuItem*>(entry))
          found.push_back(item);
      }
    }
  });
  return found;
}

Gtk::MenuItem* item_named(const std::vector<Gtk::MenuItem*>& items, const char* label)
{
  for (Gtk::MenuItem* item : items) {
    if (item->get_label() == label)
      return item;
  }
  return nullptr;
}

Gtk::TextView* text_view(Gtk::Window& window)
{
  Gtk::TextView* found = nullptr;
  walk(window, [&found](Gtk::Widget& widget) {
    if (!found)
      found = dynamic_cast<Gtk::TextView*>(&widget);
  });
  return found;
}

void activate(Gtk::MenuItem* item)
{
  if (item)
    item->activate();
  settle();
}

// One left click in a cell's text window, through GTK's event path.
void click_view(Gtk::TextView& view, double x, double y, guint time)
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
    event->button.state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
    if (pointer) {
      event->button.device = pointer->gobj();
      gdk_event_set_source_device(event, pointer->gobj());
    }
    gtk_main_do_event(event);
    gdk_event_free(event);
  };
  send(GDK_BUTTON_PRESS);
  send(GDK_BUTTON_RELEASE);
  settle();
}

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 50;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-page-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "page-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    const auto file = menu_items(window, "_File");
    const auto format = menu_items(window, "F_ormat");
    const auto insert = menu_items(window, "_Insert");
    const auto table = menu_items(window, "T_able");
    const auto view = menu_items(window, "_View");
    Gtk::MenuItem* page_setup = item_named(file, "Page Set_up…");
    Gtk::MenuItem* columns = item_named(format, "C_olumns…");
    Gtk::MenuItem* picture = item_named(insert, "_Picture…");
    Gtk::MenuItem* table_insert = item_named(insert, "_Table…");
    Gtk::MenuItem* page_break = item_named(insert, "Page _Break");
    Gtk::MenuItem* footnote = item_named(insert, "_Footnote");
    Gtk::MenuItem* insert_table = item_named(table, "_Insert Table…");
    Gtk::MenuItem* insert_row = item_named(table, "Insert _Row");
    Gtk::MenuItem* insert_column = item_named(table, "Insert _Column");
    Gtk::MenuItem* delete_row = item_named(table, "_Delete Row");
    Gtk::MenuItem* delete_column = item_named(table, "Delete C_olumn");
    Gtk::MenuItem* header = item_named(view, "_Header and Footer");
    CHECK(page_setup && page_setup->get_sensitive());
    CHECK(columns && columns->get_sensitive());
    CHECK(picture && picture->get_sensitive());
    CHECK(table_insert && table_insert->get_sensitive());
    CHECK(page_break && page_break->get_sensitive());
    CHECK(footnote && footnote->get_sensitive());
    CHECK(insert_table && insert_table->get_sensitive());
    CHECK(insert_row && !insert_row->get_sensitive());
    CHECK(insert_column && !insert_column->get_sensitive());
    CHECK(delete_row && !delete_row->get_sensitive());
    CHECK(delete_column && !delete_column->get_sensitive());
    auto* header_check = dynamic_cast<Gtk::CheckMenuItem*>(header);
    CHECK(header_check && !header_check->get_active());

    Gtk::TextView* body = text_view(window);
    CHECK(body != nullptr);
    CHECK(body && body->get_buffer()->get_char_count() == 0);

    if (body && page_break && footnote && insert_row && delete_row) {
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("Hello");
      body->get_buffer()->end_user_action();
      settle();
      CHECK(body->get_buffer()->get_text() == "Hello");

      activate(page_break);
      writeit::Document broken = writeit::MainWindowProbe::doc(window);
      CHECK(broken.paragraphs.size() == 2);
      CHECK(!broken.paragraphs[0].runs.empty() && broken.paragraphs[0].runs[0].text == "Hello");
      CHECK(!broken.paragraphs[0].page_break);
      CHECK(broken.paragraphs[1].page_break);
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document restored = writeit::MainWindowProbe::doc(window);
      CHECK(restored.paragraphs.size() == 1);
      CHECK(!restored.paragraphs[0].page_break);

      writeit::MainWindowProbe::caret(window, 0);
      activate(page_break);
      writeit::Document leading = writeit::MainWindowProbe::doc(window);
      CHECK(leading.paragraphs.size() == 1 && leading.paragraphs[0].page_break);
      CHECK(!leading.paragraphs[0].runs.empty() && leading.paragraphs[0].runs[0].text == "Hello");
      writeit::MainWindowProbe::undo(window);
      settle();

      writeit::MainWindowProbe::caret(window, 2);
      activate(page_break);
      writeit::Document split = writeit::MainWindowProbe::doc(window);
      CHECK(split.paragraphs.size() == 2);
      CHECK(!split.paragraphs[0].runs.empty() && split.paragraphs[0].runs[0].text == "He");
      CHECK(split.paragraphs[1].page_break);
      CHECK(!split.paragraphs[1].runs.empty() && split.paragraphs[1].runs[0].text == "llo");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document joined = writeit::MainWindowProbe::doc(window);
      CHECK(joined.paragraphs.size() == 1);
      CHECK(!joined.paragraphs[0].runs.empty() && joined.paragraphs[0].runs[0].text == "Hello");

      activate(footnote);
      writeit::Document noted = writeit::MainWindowProbe::doc(window);
      bool marker = false;
      for (const writeit::Run& run : noted.paragraphs[0].runs) {
        if (run.note == 1 && run.text == "1")
          marker = true;
      }
      CHECK(noted.notes.size() == 1 && marker);
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::doc(window).notes.empty());

      writeit::Document table_doc;
      writeit::insert_table(table_doc.paragraphs, 0, 2, 2, writeit::page_text_twips(table_doc.page));
      writeit::MainWindowProbe::load(window, table_doc);
      settle();
      CHECK(insert_row->get_sensitive());
      CHECK(delete_row->get_sensitive());
      const int rows_before = writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows;
      activate(insert_row);
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before + 1);
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before);
      activate(delete_row);
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before - 1);
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before);
    } else {
      for (int i = 0; i < 23; ++i)
        CHECK(false);
    }

    const char* sample_rtf =
        "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n"
        "{\\fonttbl{\\f0\\fswiss Sans;}}\n"
        "\\pard\\f0\\fs22\\b0\\i0\\ulnone This is a test\\par\n"
        "\\pard\\f0\\fs22\\b0\\i0\\ulnone\\par\n"
        "\\pard\\f0\\fs22\\b0\\i0\\ulnone This is a test\\par\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\f0\\fs22"
        "\\b0\\i0\\ulnone asdfasfdsafddsf\\tab sdadasdf\\tab asdasfdasfd\\tab \\tab dsfadfasdf"
        "\\tab \\cell\\pard\\intbl\\cell\\pard\\intbl\\cell\\pard\\intbl\\f0\\fs22\\b0\\i0"
        "\\ulnone sdasfsafdasdf\\tab \\tab wsdfasdfsafdasdfasdfasdfasdfasdfasdf\\cell\\row\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\cell"
        "\\pard\\intbl\\cell\\pard\\intbl\\cell\\pard\\intbl\\cell\\row\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\cell"
        "\\pard\\intbl\\f0\\fs22\\b0\\i0\\ulnone asdfasfasdfasfd\\cell\\pard\\intbl\\cell"
        "\\pard\\intbl\\cell\\row\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\cell"
        "\\pard\\intbl\\cell\\pard\\intbl\\cell\\pard\\intbl\\cell\\row\n"
        "\\pard\\par\n"
        "}";
    writeit::Document sample;
    CHECK(writeit::rtf_import(sample_rtf, sample));
    writeit::MainWindowProbe::load(window, sample);
    settle();
    writeit::Document shown = writeit::MainWindowProbe::doc(window);
    if (!(shown == sample)) {
      std::cerr << "table grid capture " << shown.paragraphs.size() << " paragraphs, sample "
                << sample.paragraphs.size() << "\n";
      const size_t n = std::min(shown.paragraphs.size(), sample.paragraphs.size());
      for (size_t i = 0; i < n; ++i) {
        if (!(shown.paragraphs[i] == sample.paragraphs[i])) {
          std::cerr << "first mismatch at paragraph " << i << "\n";
          break;
        }
      }
    }
    CHECK(shown.paragraphs.size() == sample.paragraphs.size());
    CHECK(shown == sample);
    int cell_views = 0;
    bool top_left = false;
    if (body) {
      for (Gtk::Widget* child : body->get_children()) {
        walk(*child, [&](Gtk::Widget& widget) {
          auto* view = dynamic_cast<Gtk::TextView*>(&widget);
          if (!view || view == body)
            return;
          ++cell_views;
          const std::string text = view->get_buffer()->get_text().raw();
          if (text.compare(0, 15, "asdfasfdsafddsf") == 0)
            top_left = true;
        });
      }
    }
    CHECK(cell_views == 16);
    CHECK(top_left);

    Gtk::TextView* cell = nullptr;
    Gtk::TextView* other = nullptr;
    if (body) {
      for (Gtk::Widget* child : body->get_children()) {
        walk(*child, [&](Gtk::Widget& widget) {
          auto* view = dynamic_cast<Gtk::TextView*>(&widget);
          if (!view || view == body)
            return;
          const std::string text = view->get_buffer()->get_text().raw();
          if (!cell && text.compare(0, 15, "asdfasfdsafddsf") == 0)
            cell = view;
          else if (!other && text.empty())
            other = view;
        });
      }
    }
    CHECK(cell != nullptr);
    CHECK(other != nullptr);
    bool cell_focus = false;
    bool still_cell = false;
    bool other_focus = false;
    bool cell_left = false;
    bool same_doc = false;
    int near = -1;
    int far = -1;
    if (cell && other) {
      auto text_win = cell->get_window(Gtk::TEXT_WINDOW_TEXT);
      const int width = text_win ? text_win->get_width() : 0;
      click_view(*cell, 4, 4, 1000);
      cell_focus = cell->has_focus();
      near = cell->get_buffer()->get_insert()->get_iter().get_offset();
      click_view(*cell, std::max(8, width - 4), 4, 2000);
      still_cell = cell->has_focus();
      far = cell->get_buffer()->get_insert()->get_iter().get_offset();
      click_view(*other, 4, 4, 3000);
      other_focus = other->has_focus();
      cell_left = !cell->has_focus();
      same_doc = writeit::MainWindowProbe::doc(window) == sample;
    }
    CHECK(cell_focus);
    CHECK(still_cell);
    CHECK(near != far);
    CHECK(other_focus);
    CHECK(cell_left);
    CHECK(same_doc);
  }
  return suite_test::done("page-ui", kChecks);
}
