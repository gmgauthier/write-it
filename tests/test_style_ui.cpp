/* SPDX-License-Identifier: Unlicense */

// The real window, under a display: the style boxes with long names, and the
// Style box over a selection. A style name of up to 255 characters (Word 97's
// limit) neither widens Format > Style... past a 1024 px screen nor makes the
// toolbar box's list as wide as the screen: the boxes show the start of the
// name. Over a selection whose paragraphs are in more than one style, the
// Style box is blank, as in Word 97. A numbered item's label is drawn in its
// text's weight, so a heading's label is bold.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

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

constexpr int kChecks = 9;

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
    window.hide();
    settle();
  }
  g_rmdir(home.c_str());
  return suite_test::done("style-ui", kChecks);
}
