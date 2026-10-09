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

#include <cstdlib>
#include <iostream>
#include <string>

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

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 22;

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

    // Saved as \fs21, the empty lines still \fs40.
    CHECK(MainWindowProbe::save(window));
    const std::string saved = Glib::file_get_contents(path);
    CHECK(contains(saved, "\\pard\\f0\\fs21\\b0\\i0\\ulnone Small\\par\n") &&
          contains(saved, "Half\\par\n\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n") &&
          contains(saved, "Small\\par\n\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n}"));

    window.hide();
    settle();
  }
  g_remove(path.c_str());
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("font-size-ui", kChecks);
}
