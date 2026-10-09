/* SPDX-License-Identifier: Unlicense */

// The real window, under a display: direct formatting made with the toolbar
// stays through named styles, as in Word 97. A word made bold keeps its bold
// when Heading 1 and then Normal are applied, and a paragraph centred by hand
// stays centred when its style is edited to centred and then to another
// alignment, while the style's other paragraphs follow it.

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
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(from), w.buffer_->get_iter_at_offset(to));
  }
  static void bold(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Bold);
  }
  static void style(MainWindow& w, const char* name)
  {
    w.apply_named_style(name);
  }
  static void align(MainWindow& w, Align align)
  {
    w.apply_align(align);
  }
  // As Format > Style...'s OK: the edited style carried to the document,
  // as one undo step.
  static bool edit_style(MainWindow& w, const Style& style)
  {
    const Document before = w.capture();
    Document after = before;
    if (!update_style(after, style.name, style))
      return false;
    w.commit_document(before, after);
    return true;
  }
};

}  // namespace writeit

namespace {

using writeit::Align;
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

// Each character's bold, as 'b' or '.'.
std::string bolds(const writeit::Paragraph& paragraph)
{
  std::string out;
  for (const writeit::Run& r : paragraph.runs)
    out += std::string(r.text.size(), r.bold ? 'b' : '.');
  return out;
}

}  // namespace

constexpr int kChecks = 7;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-style-direct-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "style-direct: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    writeit::Document doc = writeit::blank_document("Sans", 11);
    doc.paragraphs = {para("Plain bold end"), para("First"), para("Second")};
    MainWindowProbe::load(window, doc);
    settle();

    // Bold one word with the toolbar, apply Heading 1, then Normal.
    MainWindowProbe::select(window, 6, 10);
    MainWindowProbe::bold(window);
    MainWindowProbe::select(window, 0, 0);
    MainWindowProbe::style(window, "Heading 1");
    settle();
    CHECK(bolds(MainWindowProbe::doc(window).paragraphs[0]) == std::string(14, 'b'));
    MainWindowProbe::style(window, "Normal");
    settle();
    writeit::Document now = MainWindowProbe::doc(window);
    CHECK(bolds(now.paragraphs[0]) == "......bbbb....");
    CHECK(now.paragraphs[0].style == "Normal");

    // Two Heading 1 paragraphs; the first centred with the toolbar. Heading
    // 1 becomes centred, then right-aligned.
    MainWindowProbe::select(window, 15, 27);
    MainWindowProbe::style(window, "Heading 1");
    MainWindowProbe::select(window, 15, 15);
    MainWindowProbe::align(window, Align::Center);
    settle();
    writeit::Style h1 = *writeit::find_style(
        writeit::complete_sheet(writeit::style_sheet(MainWindowProbe::doc(window))), "Heading 1");
    h1.align = Align::Center;
    CHECK(MainWindowProbe::edit_style(window, h1));
    settle();
    h1.align = Align::Right;
    CHECK(MainWindowProbe::edit_style(window, h1));
    settle();
    now = MainWindowProbe::doc(window);
    CHECK(now.paragraphs[1].align == Align::Center);
    CHECK(now.paragraphs[2].align == Align::Right);
    window.hide();
    settle();
  }
  g_rmdir(home.c_str());
  return suite_test::done("style-direct", kChecks);
}
