/* SPDX-License-Identifier: Unlicense */

// The real window, under a display: the style boxes with long names, and the
// Style box over a selection. A style name of up to 255 characters (Word 97's
// limit) neither widens Format > Style... past a 1024 px screen nor makes the
// toolbar box's list as wide as the screen: the boxes show the start of the
// name. Over a selection whose paragraphs are in more than one style, the
// Style box is blank, as in Word 97. A numbered item's label is drawn in its
// text's weight, so a heading's label is bold. A style chosen while the
// caret is in a table cell styles that cell, and typing there follows it.
// A cell's highlight drops when the caret is placed in another cell or the
// body. Choosing a heading leaves the pasteboard where the caret is.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static void load(MainWindow& w, const Document& doc)
  {
    w.replace_buffer(doc, 0);
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(from), w.buffer_->get_iter_at_offset(to));
  }
  static Gtk::ComboBoxText& style_box(MainWindow& w)
  {
    return w.style_combo_;
  }
  static void style_dialog(MainWindow& w)
  {
    w.on_style_dialog();
  }
  static Pango::Weight label_weight(MainWindow& w, int paragraph)
  {
    const Document doc = w.capture();
    int width = 0;
    int gap = 0;
    auto layout =
        w.list_label_layout(doc.paragraphs[static_cast<size_t>(paragraph)], 0, 1, width, gap);
    return layout->get_font_description().get_weight();
  }
  static int find(MainWindow& w, const std::string& text)
  {
    const auto body = w.buffer_->get_text(false);
    const auto at = body.find(text);
    return at == Glib::ustring::npos ? -1 : static_cast<int>(at);
  }
  static void apply_style(MainWindow& w, const std::string& name)
  {
    w.apply_named_style(name);
  }
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  static void focus_table_cell(MainWindow& w, int which, int row, int column)
  {
    Gtk::TextView* view = w.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->get_char_count() > 0 ? buffer->end() : buffer->begin());
  }
  static bool cell_focused(MainWindow& w, int which, int row, int column)
  {
    Gtk::TextView* view = w.nth_cell_view(which, row, column);
    return view && view->has_focus();
  }
  static void set_cell_text(MainWindow& w, int which, int row, int column, const std::string& text)
  {
    if (Gtk::TextView* view = w.nth_cell_view(which, row, column))
      view->get_buffer()->set_text(text);
  }
  static void type_into_cell(MainWindow& w, int which, int row, int column, const std::string& text)
  {
    if (Gtk::TextView* view = w.nth_cell_view(which, row, column))
      view->get_buffer()->insert_at_cursor(text);
  }
  static bool cell_has_selection(MainWindow& w, int which, int row, int column)
  {
    Gtk::TextView* view = w.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return false;
    Gtk::TextIter start;
    Gtk::TextIter end;
    return view->get_buffer()->get_selection_bounds(start, end);
  }
  static void select_cell(MainWindow& w, int which, int row, int column, int from, int to)
  {
    Gtk::TextView* view = w.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    auto buffer = view->get_buffer();
    buffer->select_range(buffer->get_iter_at_offset(from), buffer->get_iter_at_offset(to));
  }
  static void focus_body(MainWindow& w)
  {
    w.text_.grab_focus();
  }
  static void place_end(MainWindow& w)
  {
    w.buffer_->place_cursor(w.buffer_->end());
  }
  static int length(MainWindow& w)
  {
    return w.buffer_->get_char_count();
  }
  static double vscroll(MainWindow& w)
  {
    return w.paste_.get_vadjustment()->get_value();
  }
  // Where the scrollbar would leave the caret: follow it no further.
  static void bold(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Bold);
  }
  static void italic(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Italic);
  }
  static void underline(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Underline);
  }
  static void scroll_bottom(MainWindow& w)
  {
    w.follow_caret_ = false;
    auto v = w.paste_.get_vadjustment();
    const double page = v->get_page_size();
    v->set_value(std::max(v->get_lower(), v->get_upper() - page));
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
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

writeit::Paragraph para(const std::string& text)
{
  writeit::Paragraph p;
  writeit::Run run;
  run.text = text;
  p.runs.push_back(run);
  return p;
}

// The widest any of the box's cells asks to be.
int cell_width(Gtk::ComboBox& box)
{
  int widest = 0;
  for (Gtk::CellRenderer* cell : box.get_cells()) {
    int minimum = 0;
    int natural = 0;
    cell->get_preferred_width(box, minimum, natural);
    widest = std::max(widest, natural);
  }
  return widest;
}

// Format > Style..., measured once it is laid out, then cancelled. The
// width of the dialog, and the most any of its boxes' entries asks for.
struct Measured {
  int dialog = -1;
  int box = -1;
};

Measured measure_style_dialog(writeit::MainWindow& window)
{
  Measured got;
  int polls = 0;
  auto poll = Glib::signal_timeout().connect(
      [&]() {
        for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
          auto* dialog = dynamic_cast<Gtk::Dialog*>(top);
          if (!dialog || !dialog->get_visible() || dialog->get_title() != "Style" ||
              dialog->get_allocated_width() <= 1)
            continue;
          got.dialog = dialog->get_allocated_width();
          std::vector<Gtk::Widget*> todo{dialog};
          while (!todo.empty()) {
            Gtk::Widget* widget = todo.back();
            todo.pop_back();
            if (auto* box = dynamic_cast<Gtk::ComboBoxText*>(widget))
              got.box = std::max(got.box, cell_width(*box));
            if (auto* container = dynamic_cast<Gtk::Container*>(widget))
              for (Gtk::Widget* child : container->get_children())
                todo.push_back(child);
          }
          dialog->response(Gtk::RESPONSE_CANCEL);
          return false;
        }
        return ++polls < 200;
      },
      50);
  MainWindowProbe::style_dialog(window);
  poll.disconnect();
  settle();
  return got;
}

}  // namespace

constexpr int kChecks = 68;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-style-ui-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "style-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    // Heading 1, Normal and Heading 2, then a 240-character style.
    writeit::Document doc = writeit::blank_document("Sans", 11);
    doc.paragraphs = {para("Title"), para("Body"), para("Part"), para("Long")};
    CHECK(writeit::apply_style(doc, 0, 0, "Heading 1") &&
          writeit::apply_style(doc, 2, 2, "Heading 2"));
    writeit::Style longest = writeit::default_styles().front();
    longest.name = std::string(240, 'W');
    longest.based_on = "Normal";
    CHECK(writeit::add_style(doc, longest) && writeit::apply_style(doc, 3, 3, longest.name));
    MainWindowProbe::load(window, doc);
    settle();

    // The Style box over a selection: one style, then three, then the caret.
    auto& box = MainWindowProbe::style_box(window);
    MainWindowProbe::select(window, 0, 3);
    settle();
    CHECK(box.get_active_text() == "Heading 1");
    MainWindowProbe::select(window, 2, 13);  // Heading 1, Normal, Heading 2
    settle();
    CHECK(box.get_active_row_number() == -1 && box.get_active_text().empty());
    MainWindowProbe::select(window, 7, 7);
    settle();
    CHECK(box.get_active_text() == "Normal");

    // Long names: the toolbar box's list, and Format > Style... with the
    // long style chosen.
    std::cout << "  toolbar list cell " << cell_width(box) << " px\n";
    CHECK(cell_width(box) <= 400);
    MainWindowProbe::select(window, 17, 17);
    settle();
    const Measured long_name = measure_style_dialog(window);
    std::cout << "  dialog " << long_name.dialog << " px, widest entry " << long_name.box
              << " px\n";
    CHECK(long_name.dialog > 0 && long_name.dialog <= 1000);
    CHECK(long_name.box > 0 && long_name.box <= 400);

    // A numbered item in Heading 1: its label is bold.
    writeit::Document listed = writeit::blank_document("Sans", 11);
    listed.paragraphs = {para("Point")};
    writeit::toggle_list(listed.paragraphs, writeit::ListKind::Number);
    writeit::apply_style(listed, 0, 0, "Heading 1");
    MainWindowProbe::load(window, listed);
    settle();
    CHECK(MainWindowProbe::label_weight(window, 0) == Pango::WEIGHT_BOLD);

    // A style chosen in the paragraph after a table styles that paragraph.
    // Counting newlines there used to style the table's cells instead.
    writeit::Document after = writeit::blank_document("Sans", 11);
    after.paragraphs = {para("Before the table")};
    CHECK(writeit::insert_table(after.paragraphs, 1, 2, 2, writeit::page_text_twips(after.page)));
    after.paragraphs.push_back(para("Initial Impressions"));
    after.paragraphs.push_back(para("The observation."));
    MainWindowProbe::load(window, after);
    settle();
    const int heading = MainWindowProbe::find(window, "Initial Impressions");
    const int observation = MainWindowProbe::find(window, "The observation.");
    CHECK(heading >= 0 && observation > heading);
    MainWindowProbe::select(window, heading,
                            heading + static_cast<int>(std::strlen("Initial Impressions")));
    MainWindowProbe::apply_style(window, "Heading 2");
    settle();
    writeit::Document styled = MainWindowProbe::doc(window);
    int heading_hits = 0;
    int cell_hits = 0;
    for (const writeit::Paragraph& paragraph : styled.paragraphs) {
      if (paragraph.style != "Heading 2")
        continue;
      if (paragraph.cell.table != 0)
        ++cell_hits;
      else
        ++heading_hits;
    }
    CHECK(heading_hits == 1);
    CHECK(cell_hits == 0);
    CHECK(styled.paragraphs.back().style == "Normal");
    MainWindowProbe::select(window, observation, observation + 4);
    MainWindowProbe::apply_style(window, "Heading 2");
    settle();
    styled = MainWindowProbe::doc(window);
    cell_hits = 0;
    bool observation_heading = false;
    for (const writeit::Paragraph& paragraph : styled.paragraphs) {
      if (paragraph.style != "Heading 2")
        continue;
      if (paragraph.cell.table != 0)
        ++cell_hits;
      for (const writeit::Run& run : paragraph.runs) {
        if (run.text.find("observation") != std::string::npos)
          observation_heading = true;
      }
    }
    CHECK(cell_hits == 0);
    CHECK(observation_heading);

    // The style combo takes the keyboard. The body caret stays on the first
    // heading, which used to receive the cell's style. Retyping follows the
    // cell's style: Heading 2 stays 14 pt bold, and Normal comes back as
    // 11 pt, not bold. A direct bold bit stays bold.
    writeit::Document table = writeit::blank_document("Sans", 11);
    table.paragraphs = {para("Title")};
    CHECK(writeit::apply_style(table, 0, 0, "Heading 1"));
    CHECK(writeit::insert_table(table.paragraphs, 1, 2, 2, writeit::page_text_twips(table.page)));
    writeit::Run imported;
    imported.text = "2.0";
    imported.bold = true;
    imported.size = 14;
    writeit::Run kept;
    kept.text = "!";
    kept.bold = true;
    kept.size = 14;
    kept.direct = writeit::kDirectBold;
    table.paragraphs[1].runs = {imported, kept};
    MainWindowProbe::load(window, table);
    window.present();
    settle();
    auto& styles = MainWindowProbe::style_box(window);
    CHECK(styles.get_active_text() == "Heading 1");
    MainWindowProbe::focus_table_cell(window, 0, 0, 0);
    settle();
    CHECK(MainWindowProbe::cell_focused(window, 0, 0, 0));
    CHECK(styles.get_active_text() == "Normal");
    CHECK(MainWindowProbe::doc(window).paragraphs[0].style == "Heading 1");
    styles.grab_focus();
    settle();
    CHECK(!MainWindowProbe::cell_focused(window, 0, 0, 0));
    CHECK(styles.get_active_text() == "Normal");
    MainWindowProbe::apply_style(window, "Heading 2");
    settle();
    writeit::Document headed = MainWindowProbe::doc(window);
    CHECK(headed.paragraphs[0].style == "Heading 1" && headed.paragraphs[0].runs[0].size == 16);
    CHECK(headed.paragraphs[1].style == "Heading 2" && headed.paragraphs[1].heading == 2);
    CHECK(headed.paragraphs[1].runs.size() == 2);
    CHECK(headed.paragraphs[1].runs[0].text == "2.0" && headed.paragraphs[1].runs[0].bold &&
          headed.paragraphs[1].runs[0].size == 14 && headed.paragraphs[1].runs[0].direct == 0);
    CHECK(headed.paragraphs[1].runs[1].text == "!" && headed.paragraphs[1].runs[1].bold &&
          headed.paragraphs[1].runs[1].size == 14 &&
          headed.paragraphs[1].runs[1].direct == writeit::kDirectBold);
    int other_styled = 0;
    for (size_t i = 2; i < headed.paragraphs.size(); ++i) {
      if (headed.paragraphs[i].style != "Normal" || headed.paragraphs[i].cell.table == 0)
        ++other_styled;
    }
    CHECK(other_styled == 0);
    CHECK(MainWindowProbe::cell_focused(window, 0, 0, 0));
    CHECK(styles.get_active_text() == "Heading 2");

    styles.grab_focus();
    settle();
    MainWindowProbe::apply_style(window, "Normal");
    settle();
    writeit::Document cleared = MainWindowProbe::doc(window);
    CHECK(cleared.paragraphs[0].style == "Heading 1");
    CHECK(cleared.paragraphs[1].style == "Normal" && cleared.paragraphs[1].heading == 0);
    CHECK(cleared.paragraphs[1].runs.size() == 2);
    CHECK(cleared.paragraphs[1].runs[0].text == "2.0" && !cleared.paragraphs[1].runs[0].bold &&
          cleared.paragraphs[1].runs[0].size == 11 && cleared.paragraphs[1].runs[0].direct == 0);
    CHECK(cleared.paragraphs[1].runs[1].text == "!" && cleared.paragraphs[1].runs[1].bold &&
          cleared.paragraphs[1].runs[1].size == 11 &&
          cleared.paragraphs[1].runs[1].direct == writeit::kDirectBold);
    CHECK(styles.get_active_text() == "Normal");

    MainWindowProbe::set_cell_text(window, 0, 0, 0, "");
    MainWindowProbe::type_into_cell(window, 0, 0, 0, "Again");
    settle();
    writeit::Document again = MainWindowProbe::doc(window);
    CHECK(again.paragraphs[0].style == "Heading 1");
    CHECK(again.paragraphs[1].style == "Normal");
    CHECK(again.paragraphs[1].runs.size() == 1 && again.paragraphs[1].runs[0].text == "Again" &&
          !again.paragraphs[1].runs[0].bold && again.paragraphs[1].runs[0].size == 11 &&
          again.paragraphs[1].runs[0].direct == 0);

    styles.grab_focus();
    settle();
    MainWindowProbe::apply_style(window, "Heading 2");
    settle();
    writeit::Document again_heading = MainWindowProbe::doc(window);
    CHECK(again_heading.paragraphs[0].style == "Heading 1");
    CHECK(again_heading.paragraphs[1].style == "Heading 2" &&
          again_heading.paragraphs[1].runs.size() == 1 &&
          again_heading.paragraphs[1].runs[0].text == "Again" &&
          again_heading.paragraphs[1].runs[0].bold &&
          again_heading.paragraphs[1].runs[0].size == 14);
    MainWindowProbe::set_cell_text(window, 0, 0, 0, "");
    MainWindowProbe::type_into_cell(window, 0, 0, 0, "Head");
    settle();
    writeit::Document retyped = MainWindowProbe::doc(window);
    CHECK(retyped.paragraphs[0].style == "Heading 1");
    CHECK(retyped.paragraphs[1].style == "Heading 2");
    CHECK(retyped.paragraphs[1].runs.size() == 1 && retyped.paragraphs[1].runs[0].text == "Head" &&
          retyped.paragraphs[1].runs[0].bold && retyped.paragraphs[1].runs[0].size == 14 &&
          retyped.paragraphs[1].runs[0].direct == 0);

    // A highlight in one cell goes when the caret is placed in another cell,
    // or in the body. It used to stay until a new block was selected.
    MainWindowProbe::focus_table_cell(window, 0, 0, 0);
    MainWindowProbe::select_cell(window, 0, 0, 0, 0, 4);
    settle();
    CHECK(MainWindowProbe::cell_has_selection(window, 0, 0, 0));
    MainWindowProbe::focus_table_cell(window, 0, 0, 1);
    settle();
    CHECK(!MainWindowProbe::cell_has_selection(window, 0, 0, 0));
    CHECK(MainWindowProbe::cell_focused(window, 0, 0, 1));
    MainWindowProbe::focus_table_cell(window, 0, 0, 0);
    MainWindowProbe::select_cell(window, 0, 0, 0, 0, 4);
    settle();
    CHECK(MainWindowProbe::cell_has_selection(window, 0, 0, 0));
    MainWindowProbe::focus_body(window);
    settle();
    CHECK(!MainWindowProbe::cell_has_selection(window, 0, 0, 0));

    // Bold, italic, and underline land in the cell. The body heading stays
    // as it was. A collapsed caret remembers the flag for the next character.
    MainWindowProbe::focus_table_cell(window, 0, 0, 1);
    MainWindowProbe::set_cell_text(window, 0, 0, 1, "Hello");
    settle();
    MainWindowProbe::select_cell(window, 0, 0, 1, 0, 5);
    MainWindowProbe::bold(window);
    settle();
    writeit::Document flagged = MainWindowProbe::doc(window);
    CHECK(flagged.paragraphs[0].runs[0].text == "Title");
    CHECK(!flagged.paragraphs[0].runs[0].underline);
    CHECK((flagged.paragraphs[0].runs[0].direct & writeit::kDirectBold) == 0);
    const writeit::Paragraph* hello = nullptr;
    for (const writeit::Paragraph& paragraph : flagged.paragraphs) {
      if (paragraph.cell.row == 0 && paragraph.cell.column == 1 && !paragraph.runs.empty() &&
          paragraph.runs[0].text == "Hello")
        hello = &paragraph;
    }
    CHECK(hello != nullptr);
    CHECK(hello && hello->runs.size() == 1 && hello->runs[0].bold && !hello->runs[0].italic &&
          !hello->runs[0].underline);
    CHECK(hello && (hello->runs[0].direct & writeit::kDirectBold) != 0);

    MainWindowProbe::select_cell(window, 0, 0, 1, 5, 5);
    MainWindowProbe::italic(window);
    MainWindowProbe::type_into_cell(window, 0, 0, 1, "!");
    settle();
    flagged = MainWindowProbe::doc(window);
    const writeit::Run* bang = nullptr;
    for (const writeit::Paragraph& paragraph : flagged.paragraphs) {
      if (paragraph.cell.row != 0 || paragraph.cell.column != 1)
        continue;
      for (const writeit::Run& run : paragraph.runs) {
        if (run.text == "!")
          bang = &run;
      }
    }
    CHECK(bang != nullptr);
    CHECK(bang && bang->bold && bang->italic && !bang->underline);
    CHECK(bang && (bang->direct & writeit::kDirectBold) != 0 &&
          (bang->direct & writeit::kDirectItalic) != 0);

    MainWindowProbe::select_cell(window, 0, 0, 1, 0, 5);
    MainWindowProbe::underline(window);
    settle();
    flagged = MainWindowProbe::doc(window);
    bool underlined = false;
    bool bang_clear = false;
    for (const writeit::Paragraph& paragraph : flagged.paragraphs) {
      if (paragraph.cell.row != 0 || paragraph.cell.column != 1)
        continue;
      for (const writeit::Run& run : paragraph.runs) {
        if (run.text == "Hello")
          underlined = run.underline && (run.direct & writeit::kDirectUnderline) != 0 && run.bold;
        if (run.text == "!")
          bang_clear = !run.underline && run.italic;
      }
    }
    CHECK(underlined);
    CHECK(bang_clear);

    MainWindowProbe::focus_table_cell(window, 0, 1, 0);
    MainWindowProbe::underline(window);
    MainWindowProbe::type_into_cell(window, 0, 1, 0, "Z");
    settle();
    flagged = MainWindowProbe::doc(window);
    bool zee = false;
    for (const writeit::Paragraph& paragraph : flagged.paragraphs) {
      if (paragraph.cell.row != 1 || paragraph.cell.column != 0)
        continue;
      for (const writeit::Run& run : paragraph.runs) {
        if (run.text == "Z" && run.underline && (run.direct & writeit::kDirectUnderline) != 0)
          zee = true;
      }
    }
    CHECK(zee);

    // Choosing a heading while the caret is down the document stays there.
    // The body insert on the first line used to pull the pasteboard to the top.
    writeit::Document letter = writeit::blank_document("Sans", 11);
    for (int i = 0; i < 80; ++i)
      letter.paragraphs.push_back(para("Paragraph " + std::to_string(i) + " of the letter."));
    MainWindowProbe::load(window, letter);
    window.resize(800, 500);
    settle();
    MainWindowProbe::place_end(window);
    settle();
    const double down = MainWindowProbe::vscroll(window);
    CHECK(down > 200);
    const int letters = MainWindowProbe::length(window);
    MainWindowProbe::select(window, std::max(0, letters - 12), letters);
    MainWindowProbe::apply_style(window, "Heading 1");
    settle();
    const double stayed = MainWindowProbe::vscroll(window);
    if (stayed < 200)
      std::cerr << "heading style scrolled to " << stayed << ", was " << down << "\n";
    CHECK(stayed > 200);

    writeit::Document below = writeit::blank_document("Sans", 11);
    for (int i = 0; i < 60; ++i)
      below.paragraphs.push_back(para("Line " + std::to_string(i) + " above the table."));
    CHECK(writeit::insert_table(below.paragraphs, below.paragraphs.size(), 2, 2,
                                writeit::page_text_twips(below.page)));
    MainWindowProbe::load(window, below);
    settle();
    MainWindowProbe::scroll_bottom(window);
    MainWindowProbe::focus_table_cell(window, 0, 0, 0);
    MainWindowProbe::set_cell_text(window, 0, 0, 0, "Cell");
    settle();
    const double at_table = MainWindowProbe::vscroll(window);
    CHECK(at_table > 200);
    MainWindowProbe::style_box(window).grab_focus();
    settle();
    MainWindowProbe::apply_style(window, "Heading 2");
    settle();
    const double at_cell = MainWindowProbe::vscroll(window);
    if (at_cell < 200)
      std::cerr << "cell heading scrolled to " << at_cell << ", was " << at_table << "\n";
    CHECK(at_cell > 200);
    window.hide();
    settle();
  }
  g_rmdir(home.c_str());
  return suite_test::done("style-ui", kChecks);
}
